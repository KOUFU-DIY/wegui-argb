#include "we_widget_dropdown.h"
#include "wegui.h"
#include <stddef.h> /* offsetof：overlay 内嵌弹层对象反推宿主 */

/* 内置配色（通过 RGB888TODEV 兼容 RGB565 / RGB888 两种色深）。 */
#if (WE_CFG_MONO_STYLE == 1)
/* 单色样式：黑底白字白边，主框按下/展开反白，高亮行反白挖空字 */
#define _DD_COL_BG          RGB888TODEV(0, 0, 0)       /* 主框背景 */
#define _DD_COL_BG_PRESSED  RGB888TODEV(255, 255, 255) /* 主框按下/展开反白 */
#define _DD_COL_POPUP_BG    RGB888TODEV(0, 0, 0)       /* 弹出列表背景 */
#define _DD_COL_BORDER      RGB888TODEV(255, 255, 255) /* 边框 */
#define _DD_COL_TEXT        RGB888TODEV(255, 255, 255) /* 正常文本 */
#define _DD_COL_TEXT_DIS    RGB888TODEV(255, 255, 255) /* 禁用文本（单色无灰阶） */
#define _DD_COL_SEL_BG      RGB888TODEV(255, 255, 255) /* 选中/高亮项反白条 */
#define _DD_COL_SEL_TEXT    RGB888TODEV(0, 0, 0)       /* 高亮项挖空字 */
#define _DD_COL_ARROW       RGB888TODEV(255, 255, 255) /* 箭头 */
#define _DD_COL_SCROLLBAR   RGB888TODEV(255, 255, 255) /* 滚动条滑块色 */
#else
#define _DD_COL_BG          RGB888TODEV(46, 52, 64)    /* 主框背景 */
#define _DD_COL_BG_PRESSED  RGB888TODEV(64, 72, 88)    /* 主框按下态背景（稍亮） */
#define _DD_COL_POPUP_BG    RGB888TODEV(58, 66, 82)    /* 弹出列表背景（比主框更亮，呈悬浮感） */
#define _DD_COL_BORDER      RGB888TODEV(76, 86, 106)   /* 边框 */
#define _DD_COL_TEXT        RGB888TODEV(229, 233, 240) /* 正常文本 */
#define _DD_COL_TEXT_DIS    RGB888TODEV(110, 118, 134) /* 禁用文本 */
#define _DD_COL_SEL_BG      RGB888TODEV(64, 152, 231)  /* 选中/高亮项背景 */
#define _DD_COL_SEL_TEXT    RGB888TODEV(255, 255, 255) /* 高亮项文本 */
#define _DD_COL_ARROW       RGB888TODEV(160, 168, 184) /* 箭头 */
#define _DD_COL_SCROLLBAR   RGB888TODEV(210, 218, 232) /* 滚动条滑块色（半透明叠加） */
#endif

/* 滚动条几何与透明度（无轨道，仅半透明胶囊滑块）。 */
#define _DD_SB_WIDTH        5   /* 滑块宽度（像素）；圆角=宽度/2，需≥4 两头半圆才明显 */
#define _DD_SB_MARGIN       3   /* 滑块距 popup 右内边距（像素） */
#define _DD_SB_THUMB_OPA    150 /* 滑块透明度（半透明） */

/* 弹出列表与主框之间的间隙（像素）。 */
#define _DD_POPUP_GAP       3

/* 判定为拖拽滚动（而非点选）的位移阈值（像素）。超过即进入无级滚动。 */
#define _DD_DRAG_THRESHOLD  5

/* 滚动物理参数：无惯性档（惯性分子/分母占位不使用——松手前速度清零，
 * 组件步进只走过冲回弹段） */
static const we_scroll_cfg_t _dd_scroll_cfg = {
    _DD_DRAG_THRESHOLD,          WE_DROPDOWN_OVERSCROLL_LIMIT,
    1, 1,
    WE_DROPDOWN_REBOUND_PULL_DIV, WE_DROPDOWN_REBOUND_MAX_STEP,
    128,
};

/**
 * @brief 计算 popup 区域并写入 overlay 几何：优先向下展开，下方不足则向上，
 *        均不足取空间更大方向并按整数项限高。
 * @param d 下拉控件指针。
 * @return 无。
 */
static void _dropdown_layout_overlay(we_dropdown_obj_t *d)
{
    uint16_t rows = d->option_cnt;
    int16_t ph;
    int16_t scr_h = (int16_t)d->base.lcd->height;
    /* 预留与主框之间的间隙后，上/下方可用空间。 */
    int16_t below = (int16_t)(scr_h - (d->base.y + d->base.h) - _DD_POPUP_GAP);
    int16_t above = (int16_t)(d->base.y - _DD_POPUP_GAP);
    int16_t py;

    if (rows > d->max_visible_items)
        rows = d->max_visible_items;
    if (rows == 0U)
        rows = 1U;
    ph = (int16_t)(rows * d->item_h);

    if (ph <= below)
    {
        py = (int16_t)(d->base.y + d->base.h + _DD_POPUP_GAP); /* 向下展开，留间隙 */
    }
    else if (ph <= above)
    {
        py = (int16_t)(d->base.y - _DD_POPUP_GAP - ph); /* 向上展开，留间隙 */
    }
    else
    {
        /* 上下都不足：取空间更大方向，并按整数项限高（至少 1 项）。 */
        int16_t space = (below >= above) ? below : above;
        uint16_t fit = (uint16_t)(space / (int16_t)d->item_h);
        if (fit == 0U)
            fit = 1U;
        if (fit > rows)
            fit = rows;
        ph = (int16_t)(fit * d->item_h);
        py = (below >= above) ? (int16_t)(d->base.y + d->base.h + _DD_POPUP_GAP)
                              : (int16_t)(d->base.y - _DD_POPUP_GAP - ph);
    }

    d->overlay.x = d->base.x;
    d->overlay.y = py;
    d->overlay.w = d->base.w;
    d->overlay.h = ph;
}

/**
 * @brief 在指定文本框内垂直居中绘制一行文本（超出文本框的部分被裁掉）。
 * @param lcd GUI 上下文。
 * @param font 字体资源。
 * @param text 文本。
 * @param color 文本颜色。
 * @param box_x 文本框左上 X（文本左对齐起点 = box_x + pad）。
 * @param box_y 文本框左上 Y。
 * @param box_w 文本框宽度。
 * @param box_h 文本框高度。
 * @param pad 左内边距。
 * @return 无。
 */
static void _dropdown_draw_text_clipped(we_lcd_t *lcd, const unsigned char *font,
                                        const char *text, colour_t color,
                                        int16_t box_x, int16_t box_y,
                                        int16_t box_w, int16_t box_h, int16_t pad)
{
    we_pfb_clip_t sv;

    if (text == NULL || font == NULL)
        return;

    we_pfb_clip_save(lcd, &sv);
    if (we_pfb_clip_to(lcd, &sv, box_x, box_y,
                       (int16_t)(box_x + box_w - 1), (int16_t)(box_y + box_h - 1)))
    {
        int8_t y_top, y_bot;

        /* 与当前窗口相交才测量文字（竖直居中按文字实际墨迹上下沿） */
        we_get_text_bbox(font, text, &y_top, &y_bot);
        we_draw_string(lcd, (int16_t)(box_x + pad),
                       (int16_t)(box_y + box_h / 2 - (y_top + y_bot) / 2),
                       font, text, color, 255U);
        we_pfb_clip_restore(lcd, &sv);
    }
}

/**
 * @brief 在主框右侧绘制实心三角箭头（展开朝上▲，收起朝下▼）。
 *
 * 采用解析覆盖率：像素内 4x4 子采样，统计落在三角形内的子样本数作为
 * 该像素覆盖率（0~16），再映射成 alpha 与背景混合。坐标统一放大 8 倍，
 * 子样本落在像素内 1/8、3/8、5/8、7/8 处；纯整数 edge-function 判定，
 * 尖角是真正收敛的数学尖点，边缘为连续灰阶抗锯齿。只计算包围盒
 * （11x9 像素）与当前窗口相交的像素。
 * @param d 下拉控件指针。
 * @return 无。
 */
static void _dropdown_draw_arrow(we_dropdown_obj_t *d)
{
    we_lcd_t *lcd = d->base.lcd;
    int16_t cx = (int16_t)(d->base.x + d->base.w - 13);
    int16_t cy = (int16_t)(d->base.y + d->base.h / 2);
    int16_t hw = 5;  /* 水平半宽 */
    int16_t vh = 4;  /* 三角高度方向半幅 */
    colour_t col = _DD_COL_ARROW;
    int32_t ax, ay, bx, by, tx, ty; /* 左角 / 右角 / 尖点（×8） */
    int16_t x0, y0, x1, y1;
    int16_t px, py;

    if (d->opened)
    {
        /* 朝上▲：底边两角在下，尖点在上 */
        ax = (int32_t)(cx - hw) * 8; ay = (int32_t)(cy + vh) * 8;
        bx = (int32_t)(cx + hw) * 8; by = (int32_t)(cy + vh) * 8;
        tx = (int32_t)cx * 8;        ty = (int32_t)(cy - vh) * 8;
    }
    else
    {
        /* 朝下▼：顶边两角在上，尖点在下 */
        ax = (int32_t)(cx - hw) * 8; ay = (int32_t)(cy - vh) * 8;
        bx = (int32_t)(cx + hw) * 8; by = (int32_t)(cy - vh) * 8;
        tx = (int32_t)cx * 8;        ty = (int32_t)(cy + vh) * 8;
    }

    x0 = WE_MAX((int16_t)(cx - hw), lcd->clip.x0);
    x1 = WE_MIN((int16_t)(cx + hw), lcd->clip.x1);
    y0 = WE_MAX((int16_t)(cy - vh), lcd->clip.y0);
    y1 = WE_MIN((int16_t)(cy + vh), lcd->clip.y1);

    for (py = y0; py <= y1; py++)
    {
        for (px = x0; px <= x1; px++)
        {
            uint8_t inside = 0U;
            int16_t sx, sy;

            for (sy = 0; sy < 4; sy++)
            {
                int32_t spy = (int32_t)py * 8 + 1 + 2 * sy;
                for (sx = 0; sx < 4; sx++)
                {
                    int32_t spx = (int32_t)px * 8 + 1 + 2 * sx;
                    /* 三条边的 edge-function 同号 → 点在三角形内。 */
                    int32_t e0 = (bx - ax) * (spy - ay) - (by - ay) * (spx - ax);
                    int32_t e1 = (tx - bx) * (spy - by) - (ty - by) * (spx - bx);
                    int32_t e2 = (ax - tx) * (spy - ty) - (ay - ty) * (spx - tx);
                    if ((e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                        (e0 <= 0 && e1 <= 0 && e2 <= 0))
                        inside++;
                }
            }

            if (inside != 0U)
            {
                /* 覆盖率 0~16 → alpha 0~255。 */
                uint8_t a = (uint8_t)(((uint16_t)inside * 255U) >> 4);
                we_draw_pixel(lcd, px, py, col, a);
            }
        }
    }
}

/**
 * @brief 主框绘制回调：背景圆角矩形 + 当前选中文本 + 方向箭头。
 * @param ptr 控件对象指针。
 * @return 无。
 */
static void _dropdown_draw_cb(void *ptr)
{
    we_dropdown_obj_t *d = (we_dropdown_obj_t *)ptr;
    we_lcd_t *lcd = d->base.lcd;
    const char *txt = NULL;
    colour_t bg = (d->pressed || d->opened) ? _DD_COL_BG_PRESSED : _DD_COL_BG;

    we_draw_round_rect_analytic_fill(lcd, d->base.x, d->base.y, d->base.w, d->base.h,
                                     d->radius, bg, 255U);

#if (WE_CFG_MONO_STYLE == 1)
    /* 单色样式：常态给主框补 1px 白描边（黑底黑框否则不可见） */
    if (!(d->pressed || d->opened))
        we_draw_round_rect_outline_fill(lcd, d->base.x, d->base.y, d->base.w, d->base.h,
                                        d->radius, _DD_COL_BORDER, _DD_COL_BG, 255U);
#endif

    if (d->selected_idx >= 0 && d->selected_idx < (int16_t)d->option_cnt &&
        d->options != NULL)
    {
        txt = d->options[d->selected_idx].text;
    }
    if (txt != NULL)
    {
#if (WE_CFG_MONO_STYLE == 1)
        colour_t tc = (d->pressed || d->opened) ? _DD_COL_SEL_TEXT : _DD_COL_TEXT; /* 反白态挖空字 */
#else
        colour_t tc = d->enabled ? _DD_COL_TEXT : _DD_COL_TEXT_DIS;
#endif
        _dropdown_draw_text_clipped(lcd, d->font, txt, tc,
                                    d->base.x, d->base.y,
                                    (int16_t)(d->base.w - 22), d->base.h, 8);
    }

    _dropdown_draw_arrow(d);
}

/**
 * @brief 内容总高度与可视高度之差，即最大可滚动像素（>=0）。
 * @param d 下拉控件指针。
 * @return 最大滚动像素，内容不溢出时为 0。
 */
static int32_t _dropdown_max_scroll(we_dropdown_obj_t *d)
{
    int32_t m = (int32_t)d->option_cnt * (int32_t)d->item_h - (int32_t)d->overlay.h;
    return (m > 0) ? m : 0;
}

/**
 * @brief 只标脏 popup 右缘滚动条条带（滑块 + 边距），淡出/唤醒专用。
 * @param d 下拉控件指针。
 * @return 无。
 * @note 滚动条 alpha 变化不触碰行内容，整层标脏纯属浪费；条带宽
 *       = _DD_SB_WIDTH + _DD_SB_MARGIN，高取弹层全高（覆盖轨道退化情形）。
 */
static void _dropdown_invalidate_sb_strip(we_dropdown_obj_t *d)
{
    int16_t strip_w = (int16_t)(_DD_SB_WIDTH + _DD_SB_MARGIN);

    we_obj_invalidate_area(&d->overlay, (int16_t)(d->overlay.x + d->overlay.w - strip_w),
                           d->overlay.y, strip_w, d->overlay.h);
}

/**
 * @brief 只标脏 popup 中某选项的行条带（与弹层区域求交）。
 * @param d 下拉控件指针。
 * @param idx 选项索引（<0、越界或已滚出可视区时空操作）。
 * @return 无。
 * @note hover 高亮变化只影响单行，按行条带标脏替代整层标脏；
 *       首/末行与面板圆角重叠的像素同在条带内，重绘时正确合成。
 */
static void _dropdown_invalidate_row(we_dropdown_obj_t *d, int16_t idx)
{
    int32_t top = (int32_t)d->overlay.y;
    int32_t bottom = top + (int32_t)d->overlay.h - 1;
    int32_t row_y0;
    int32_t row_y1;

    if (idx < 0 || idx >= (int16_t)d->option_cnt)
        return;

    row_y0 = top + (int32_t)idx * (int32_t)d->item_h - d->sc.pos;
    row_y1 = row_y0 + (int32_t)d->item_h - 1;
    if (row_y0 < top)
        row_y0 = top;
    if (row_y1 > bottom)
        row_y1 = bottom;
    if (row_y0 > row_y1)
        return; /* 行已滚出可视区 */

    we_obj_invalidate_area(&d->overlay, d->overlay.x, (int16_t)row_y0,
                           d->overlay.w, (int16_t)(row_y1 - row_y0 + 1));
}

static void _dropdown_sb_fade_step_cb(void *owner, uint16_t elapsed_ms);

/**
 * @brief 唤醒滚动条：透明度拉满、空闲计时清零（随后由淡出动画自动隐藏）。
 *        仅当内容确实可滚动时才生效。
 * @param d 下拉控件指针。
 * @return 无。
 */
static void _dropdown_sb_wake(we_dropdown_obj_t *d)
{
    if (_dropdown_max_scroll(d) == 0)
        return; /* 不可滚动，无需显示滚动条 */
    d->sb_idle_ms = 0U;
    if (d->sb_alpha != _DD_SB_THUMB_OPA)
    {
        d->sb_alpha = _DD_SB_THUMB_OPA;
        _dropdown_invalidate_sb_strip(d); /* 只标滚动条条带 */
    }
    /* 重新挂入淡出动画（收敛/收起后节点会自行摘链，这里再次唤醒） */
    we_anim_start(d->base.lcd, &d->sb_anim, _dropdown_sb_fade_step_cb, d);
}

/**
 * @brief 滚动位置变化后的统一提交：唤醒滚动条并重绘列表。
 * @param d 下拉控件指针。
 * @return 无。
 */
static void _dropdown_scroll_commit(we_dropdown_obj_t *d)
{
    _dropdown_sb_wake(d); /* 滚动即唤醒滚动条 */
    we_obj_invalidate(&d->overlay);
}

/**
 * @brief 程序化滚动定位：硬夹紧到 [0, max_scroll]，位置有变化才提交。
 * @param d 下拉控件指针。
 * @param new_scroll 目标滚动像素。
 * @return 无。
 */
static void _dropdown_scroll_to(we_dropdown_obj_t *d, int32_t new_scroll)
{
    if (we_scroll_set(&d->sc, new_scroll, _dropdown_max_scroll(d)))
        _dropdown_scroll_commit(d);
}

/**
 * @brief 停止越界回弹动画（摘链停表）。
 * @param d 下拉控件指针。
 * @return 无。
 */
static void _dropdown_stop_rebound(we_dropdown_obj_t *d)
{
    d->sc.animating = 0U;
    we_anim_stop(d->base.lcd, &d->rb_anim);
}

/**
 * @brief 越界回弹动画步进：每步拉回"过冲/WE_DROPDOWN_REBOUND_PULL_DIV"
 *        （1..MAX_STEP px），整数缓动天然先快后慢，回到边界即摘链。
 * @param owner 控件对象指针（we_anim_t.owner 透传）。
 * @param elapsed_ms 本次步进经过的毫秒数。
 * @return 无。
 */
static void _dropdown_rebound_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_dropdown_obj_t *d = (we_dropdown_obj_t *)owner;

    if (d == NULL || elapsed_ms == 0U)
        return;
    if (!d->opened || !d->sc.animating)
    {
        _dropdown_stop_rebound(d); /* 已收起/被打断：摘链停表 */
        return;
    }

    if (we_scroll_anim_step(&d->sc, &_dd_scroll_cfg, elapsed_ms, _dropdown_max_scroll(d)))
        _dropdown_scroll_commit(d);

    if (!d->sc.animating)
        _dropdown_stop_rebound(d); /* 回到边界内，收敛摘链 */
}

/**
 * @brief 松手/取消后按需启动回弹（当前滚动越界才启动）。
 * @param d 下拉控件指针。
 * @return 无。
 */
static void _dropdown_start_rebound_if_needed(we_dropdown_obj_t *d)
{
    d->sc.vel = 0; /* 无惯性档：仅越界时回弹 */
    if (we_scroll_release(&d->sc, _dropdown_max_scroll(d)))
        we_anim_start(d->base.lcd, &d->rb_anim, _dropdown_rebound_step_cb, d);
}

/**
 * @brief 滚动条淡出动画步进（中央动画引擎驱动）：popup 打开且滚动条可见时，
 *        空闲超过 HOLD 后按 FADE 时长线性淡出到最低常驻透明度。
 * @param owner 控件对象指针。
 * @param elapsed_ms 本次步进经过的毫秒数。
 * @return 无。
 */
static void _dropdown_sb_fade_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_dropdown_obj_t *d = (we_dropdown_obj_t *)owner;
    uint32_t step;

    if (d == NULL || elapsed_ms == 0U)
        return;
    if (!d->opened || d->sb_alpha <= WE_DROPDOWN_SB_IDLE_ALPHA)
    {
        /* 已收起或已收敛到常驻最低透明度：摘链停表，等待下次 wake 重新挂入 */
        we_anim_stop(d->base.lcd, &d->sb_anim);
        return;
    }

    /* 拖拽中保持完全显示，不淡出 */
    if (d->sc.dragging)
    {
        d->sb_idle_ms = 0U;
        return;
    }

    /* 累计空闲时间（饱和到 HOLD+FADE，避免 uint16 溢出） */
    if ((uint32_t)d->sb_idle_ms + elapsed_ms >= (uint32_t)(WE_DROPDOWN_SB_HOLD_MS + WE_DROPDOWN_SB_FADE_MS))
        d->sb_idle_ms = (uint16_t)(WE_DROPDOWN_SB_HOLD_MS + WE_DROPDOWN_SB_FADE_MS);
    else
        d->sb_idle_ms = (uint16_t)(d->sb_idle_ms + elapsed_ms);

    if (d->sb_idle_ms <= WE_DROPDOWN_SB_HOLD_MS)
        return; /* 仍在保持期，维持峰值 */

    /* 进入淡出期：alpha 从峰值线性递减到常驻最低值 IDLE_ALPHA（不到 0）。
     * 步长按"峰值→0"的全程速率，使淡出手感与 FADE_MS 一致。 */
    step = (uint32_t)_DD_SB_THUMB_OPA * (uint32_t)elapsed_ms / (uint32_t)WE_DROPDOWN_SB_FADE_MS;
    if (step == 0U)
        step = 1U; /* 保证慢主循环下也能前进 */

    if ((uint32_t)d->sb_alpha > (uint32_t)WE_DROPDOWN_SB_IDLE_ALPHA + step)
        d->sb_alpha = (uint8_t)((uint32_t)d->sb_alpha - step);
    else
        d->sb_alpha = WE_DROPDOWN_SB_IDLE_ALPHA; /* 收敛到常驻最低值 */

    _dropdown_invalidate_sb_strip(d); /* 淡出每帧只标滚动条条带 */
}

/**
 * @brief 在 popup 圆角矩形 mask 内填充某一选项行的高亮背景。
 *
 * 直接 we_fill_rect 会画出直角，首/末项会溢出 popup 的圆角；这里对行内
 * 每个像素取 popup 整体圆角矩形的覆盖率，使高亮边缘自然跟随圆角。
 * @param d 下拉控件指针。
 * @param iy 行顶部 Y。
 * @param color 高亮色。
 * @return 无。
 */
static void _dropdown_fill_item_rounded(we_dropdown_obj_t *d, int16_t iy, colour_t color)
{
    we_lcd_t *lcd = d->base.lcd;
    int16_t ax0 = d->overlay.x;
    int16_t ay0 = d->overlay.y;
    int16_t ax1 = (int16_t)(ax0 + d->overlay.w - 1);
    int16_t ay1 = (int16_t)(ay0 + d->overlay.h - 1);
    int16_t pw = d->overlay.w;
    int16_t ph = d->overlay.h;
    uint16_t r = d->radius;
    int16_t row_y1 = (int16_t)(iy + (int16_t)d->item_h - 1);
    int16_t px, py;

    if (r > (uint16_t)pw / 2U) r = (uint16_t)(pw / 2U);
    if (r > (uint16_t)ph / 2U) r = (uint16_t)(ph / 2U);

    /* 行的 y 范围夹紧到当前窗口（窗口已在 popup 之内），
     * 既防越界写像素，又省掉窗口外的覆盖率计算。 */
    if (iy < lcd->clip.y0)
        iy = lcd->clip.y0;
    if (row_y1 > lcd->clip.y1)
        row_y1 = lcd->clip.y1;
    if (iy > row_y1)
        return;

    /* 圆角只影响 popup 顶部/底部各 r 行；滚动时绝大多数高亮行
     * 不与圆角带相交，整块直填（we_fill_rect 行级写入），
     * 只有圆角带内的行才对左右各 r 列查覆盖率，行中段仍直填。 */
    {
        int16_t top_band_y1 = (int16_t)(ay0 + (int16_t)r - 1);
        int16_t bot_band_y0 = (int16_t)(ay1 - (int16_t)r + 1);
        int16_t mid_y0 = (iy > (int16_t)(top_band_y1 + 1)) ? iy : (int16_t)(top_band_y1 + 1);
        int16_t mid_y1 = (row_y1 < (int16_t)(bot_band_y0 - 1)) ? row_y1 : (int16_t)(bot_band_y0 - 1);

        if (mid_y0 <= mid_y1)
            we_fill_rect(lcd, ax0, mid_y0, (uint16_t)pw,
                         (uint16_t)(mid_y1 - mid_y0 + 1), color, 255U);

        for (py = iy; py <= row_y1; py++)
        {
            if (py >= mid_y0 && py <= mid_y1)
                continue; /* 中段行已整块填过 */

            for (px = ax0; px < (int16_t)(ax0 + (int16_t)r); px++)
            {
                uint8_t m = we_mask_round_rect_alpha(ax0, ay0, (uint16_t)pw,
                                                     (uint16_t)ph, r, px, py);
                if (m != 0U)
                    we_draw_pixel(lcd, px, py, color, m);
            }

            if (pw > (int16_t)(2U * r))
                we_fill_rect(lcd, (int16_t)(ax0 + (int16_t)r), py,
                             (uint16_t)((uint16_t)pw - 2U * r), 1U, color, 255U);

            for (px = (int16_t)(ax1 - (int16_t)r + 1); px <= ax1; px++)
            {
                uint8_t m = we_mask_round_rect_alpha(ax0, ay0, (uint16_t)pw,
                                                     (uint16_t)ph, r, px, py);
                if (m != 0U)
                    we_draw_pixel(lcd, px, py, color, m);
            }
        }
    }
}

/**
 * @brief 在 popup 右侧绘制半透明滚动条（无轨道，仅胶囊滑块）。
 *
 * 仅当内容总高超过可视高度时显示。滑块高度按“可视高/内容总高”比例，
 * 位置按滚动量在可滚动像素范围内的占比。纯绘制，不参与交互。
 * @param d 下拉控件指针。
 * @return 无。
 */
static void _dropdown_draw_scrollbar(we_dropdown_obj_t *d)
{
    we_lcd_t *lcd = d->base.lcd;
    int16_t ph = d->overlay.h;
    int32_t content_h = (int32_t)d->option_cnt * (int32_t)d->item_h;
    int32_t max_scroll;
    int16_t track_x;
    int16_t track_y0;
    int16_t track_h;
    int16_t thumb_h;
    int16_t thumb_y;

    if (d->sb_alpha == 0U)
        return; /* alpha 为 0（未唤醒/不可滚动）时不绘制 */

    /* 轨道：上下各留 radius 像素，避免压到圆角；宽度固定。 */
    track_x = (int16_t)(d->overlay.x + d->overlay.w - _DD_SB_MARGIN - _DD_SB_WIDTH);
    track_y0 = (int16_t)(d->overlay.y + (int16_t)d->radius);
    track_h = (int16_t)(ph - 2 * (int16_t)d->radius);
    if (track_h < (int16_t)d->item_h)
    {
        track_y0 = d->overlay.y;
        track_h = ph; /* 圆角过大时退化为整高轨道 */
    }

    /* 滑块总在轨道之内：轨道与当前窗口不相交就不必再算滑块几何 */
    if (track_x > lcd->clip.x1 || (int16_t)(track_x + _DD_SB_WIDTH - 1) < lcd->clip.x0 ||
        track_y0 > lcd->clip.y1 || (int16_t)(track_y0 + track_h - 1) < lcd->clip.y0)
        return;

    max_scroll = _dropdown_max_scroll(d);
    if (max_scroll == 0 || content_h == 0)
        return; /* 无需滚动 */

    /* 滑块几何：高按可视比例（下限 8px），位置按夹紧后的滚动进度插值。 */
    we_scroll_bar_geom(track_y0, track_h, ph, content_h,
                       d->sc.pos, max_scroll, 8, &thumb_y, &thumb_h);

    /* 仅绘制半透明胶囊滑块，不画轨道；圆角=半宽 → 两头自然收成半圆。
     * 透明度取当前淡出值 sb_alpha（由滚动条淡出动画节点推进）。 */
    we_draw_round_rect_analytic_fill(lcd, track_x, thumb_y, _DD_SB_WIDTH,
                                     (uint16_t)thumb_h, _DD_SB_WIDTH / 2U,
                                     _DD_COL_SCROLLBAR, d->sb_alpha);
}

/**
 * @brief popup 绘制回调：列表背景圆角矩形 + 可见项文本（含高亮/禁用）+ 滚动条。
 * @param d 控件对象指针（由 overlay 内嵌弹层对象反推）。
 * @return 无。
 * @note 内核已把窗口收窄到 popup 区域，半露行的文本/高亮越不过 popup 边缘；
 *       只处理与当前切片相交的行。
 */
static void _dropdown_popup_draw(we_dropdown_obj_t *d)
{
    we_lcd_t *lcd = d->base.lcd;
    int32_t first = d->sc.pos / (int32_t)d->item_h; /* 第一个（可能半露）可见项 */
    int16_t iy;                                     /* 当前行顶部 Y */
    uint16_t idx;

    /* 列表整体背景：用比主框更亮的悬浮色，圆角与主框一致。 */
    we_draw_round_rect_analytic_fill(lcd, d->overlay.x, d->overlay.y,
                                     (uint16_t)d->overlay.w, (uint16_t)d->overlay.h,
                                     d->radius, _DD_COL_POPUP_BG, 255U);

    /* 无级滚动：顶部过冲时首项下移，露出上方空白 */
    if (first < 0)
        first = 0;
    iy = (int16_t)((int32_t)d->overlay.y + first * (int32_t)d->item_h - d->sc.pos);

    for (idx = (uint16_t)first; idx < d->option_cnt; idx++, iy = (int16_t)(iy + (int16_t)d->item_h))
    {
        const we_dropdown_option_t *op = &d->options[idx];
        colour_t tc;

        if (iy > lcd->clip.y1)
            break; /* 以下各行都在当前切片之下 */
        if ((int16_t)(iy + (int16_t)d->item_h - 1) < lcd->clip.y0)
            continue; /* 本行在当前切片之上 */

        if ((int16_t)idx == d->hover_idx)
        {
            /* 按下高亮：整行铺选中背景色，并跟随 popup 圆角裁剪。 */
            _dropdown_fill_item_rounded(d, iy, _DD_COL_SEL_BG);
            tc = _DD_COL_SEL_TEXT;
        }
        else if (op->disabled)
        {
            tc = _DD_COL_TEXT_DIS;
        }
        else if ((int16_t)idx == d->selected_idx)
        {
            tc = _DD_COL_SEL_BG; /* 当前选中项用强调色文本标识 */
        }
        else
        {
            tc = _DD_COL_TEXT;
        }

        _dropdown_draw_text_clipped(lcd, d->font, op->text, tc,
                                    d->overlay.x, iy, d->overlay.w, (int16_t)d->item_h, 8);
    }

    /* 内容超出可视高度时，叠加半透明滚动条。 */
    _dropdown_draw_scrollbar(d);
}

/**
 * @brief 主框按压态变化：状态变了才标脏主框。
 * @param d 控件对象指针。
 * @param pressed 新的按压态（0/1）。
 * @return 无。
 */
static void _dropdown_set_pressed(we_dropdown_obj_t *d, uint8_t pressed)
{
    if (d->pressed != pressed)
    {
        d->pressed = pressed;
        we_obj_invalidate((we_obj_t *)d);
    }
}

/**
 * @brief 选中一项并通知业务层（越界或禁用项不选）。
 * @param d 控件对象指针。
 * @param idx 选项索引。
 * @return 1 表示已选中，0 表示未选。
 */
static uint8_t _dropdown_pick(we_dropdown_obj_t *d, int16_t idx)
{
    if (idx < 0 || idx >= (int16_t)d->option_cnt || d->options[idx].disabled)
        return 0U;
    we_dropdown_set_selected(d, idx);
    if (d->changed_cb != NULL)
        d->changed_cb(d, idx, d->options[idx].value);
    return 1U;
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_DROPDOWN_USE_KEY == 1)
static uint8_t _dropdown_key_cb(void *ptr, uint8_t key_evt);
#endif

/**
 * @brief 主框事件回调：点击主框切换展开/收起。
 * @param ptr 控件对象指针。
 * @param event 输入事件。
 * @param data 输入数据。
 * @return 1 表示消费事件。
 */
static uint8_t _dropdown_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_DROPDOWN_USE_KEY == 1)
    /* 统一事件通道：语义键/焦点通知（0x10+）分流到键处理器 */
    if ((uint8_t)event >= WE_KEY_UP)
        return _dropdown_key_cb(ptr, (uint8_t)event);
#endif

    we_dropdown_obj_t *d = (we_dropdown_obj_t *)ptr;
    (void)data;

    if (!d->enabled)
        return 1U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        _dropdown_set_pressed(d, 1U);
        break;
    case WE_EVENT_RELEASED:
        _dropdown_set_pressed(d, 0U);
        break;
    case WE_EVENT_CLICKED:
        d->pressed = 0U;
        we_dropdown_toggle(d); /* toggle 内部会标脏主框 */
        break;
    default:
        break;
    }

    return 1U; /* 主框始终消费事件 */
}

/**
 * @brief 命中测试：返回 popup 内的点 (px,py) 对应的选项索引，无对应项返回 -1。
 * @param d 下拉控件指针。
 * @param py 触摸 Y（调用方已确认触点在 popup 内）。
 * @return 选项索引或 -1。
 */
static int16_t _dropdown_popup_hit(we_dropdown_obj_t *d, int16_t py)
{
    /* 触摸点映射到内容坐标系（含滚动偏移），再整除项高得到索引。 */
    int32_t content_y = (int32_t)(py - d->overlay.y) + d->sc.pos;
    int32_t idx;

    if (content_y < 0)
        return -1; /* 顶部过冲露出的空白 */
    idx = content_y / (int32_t)d->item_h;
    return (idx < (int32_t)d->option_cnt) ? (int16_t)idx : -1;
}

/**
 * @brief popup 事件回调：拖拽滚动 / 选项选择 / 点击外部关闭。
 * @param d 控件对象指针。
 * @param event 输入事件。
 * @param data 输入数据。
 * @return 1 表示消费事件（阻止穿透到下层控件）。
 */
static uint8_t _dropdown_popup_event(we_dropdown_obj_t *d, we_event_t event, we_indev_data_t *data)
{
    uint8_t inside = (data->x >= d->overlay.x && data->x < d->overlay.x + d->overlay.w &&
                      data->y >= d->overlay.y && data->y < d->overlay.y + d->overlay.h) ? 1U : 0U;

    if (event == WE_EVENT_PRESSED)
    {
        _dropdown_stop_rebound(d); /* 按住即停回弹（越界定格，松手再回弹） */
        we_scroll_press(&d->sc, data->y);
        if (inside)
        {
            int16_t hit = _dropdown_popup_hit(d, data->y);

            if (hit != d->hover_idx)
            {
                int16_t old_hover = d->hover_idx;

                d->hover_idx = hit;
                _dropdown_invalidate_row(d, old_hover); /* 只标新旧两个行条带 */
                _dropdown_invalidate_row(d, hit);
            }
        }
        return 1U;
    }
    else if (event == WE_EVENT_STAY)
    {
        int32_t old_pos = d->sc.pos;
        uint8_t r = we_scroll_stay(&d->sc, &_dd_scroll_cfg, data->y,
                                   _dropdown_max_scroll(d));

        if (r == 1U && d->hover_idx != -1)
        {
            int16_t old_hover = d->hover_idx;

            d->hover_idx = -1; /* 拖拽中取消按下高亮 */
            _dropdown_invalidate_row(d, old_hover); /* 只标该行条带 */
        }
        if (r != 0U && d->sc.pos != old_pos)
            _dropdown_scroll_commit(d); /* 跟手（组件软夹紧允许过冲） */
        return 1U;
    }
    else if (event == WE_EVENT_RELEASED)
    {
        int16_t sel = d->hover_idx;
        uint8_t was_drag = d->sc.dragging;
        d->hover_idx = -1;
        d->sc.dragging = 0U;
        if (was_drag)
        {
            /* 拖拽滚动结束：保持展开，不选择、不关闭，仅清掉按下高亮
             * （拖拽起步时高亮通常已清，这里按行条带兜底）；
             * 越界过冲则启动回弹动画。 */
            _dropdown_invalidate_row(d, sel);
            _dropdown_start_rebound_if_needed(d);
            return 1U;
        }
        if (!inside)
        {
            /* 点击外部：关闭，消费事件避免穿透到下层控件。 */
            we_dropdown_close(d);
            return 1U;
        }
        if (_dropdown_pick(d, sel))
        {
            we_dropdown_close(d);
        }
        else
        {
            _dropdown_invalidate_row(d, sel);     /* 禁用项：只标该行清除高亮 */
            _dropdown_start_rebound_if_needed(d); /* 定格在过冲区的轻点松开 */
        }
        return 1U;
    }

    return 1U;
}

/* --------------------------------------------------------------------------
 * 展开列表内嵌弹层对象（顶层链成员，模态）
 * 由内嵌 overlay 反推宿主 dropdown；绘制/触摸/按键包装到既有弹层处理器。
 * -------------------------------------------------------------------------- */
#define _DD_FROM_OVERLAY(p) \
    ((we_dropdown_obj_t *)(void *)((char *)(p) - offsetof(we_dropdown_obj_t, overlay)))

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_DROPDOWN_USE_KEY == 1)
static uint8_t _dropdown_popup_key_cb(we_dropdown_obj_t *d, uint8_t key);
#endif

static void _dd_overlay_draw(void *ptr)
{
    _dropdown_popup_draw(_DD_FROM_OVERLAY(ptr));
}

static uint8_t _dd_overlay_event(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_dropdown_obj_t *d = _DD_FROM_OVERLAY(ptr);

    if (event == WE_EVENT_MODAL_CLOSE)
    {
        we_dropdown_close(d); /* 被新模态替换：自行收起 */
        return 1U;
    }
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_DROPDOWN_USE_KEY == 1)
    if ((uint8_t)event >= WE_KEY_UP)
        return _dropdown_popup_key_cb(d, (uint8_t)event);
#endif
    return _dropdown_popup_event(d, event, data);
}

static const we_class_t _dd_overlay_class = {
    .draw_cb = _dd_overlay_draw,
    .event_cb = _dd_overlay_event,
};

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_DROPDOWN_USE_KEY == 1)
/**
 * @brief 键控移动展开列表的高亮行：新旧行条带标脏 + 滚动跟随。
 * @param d 控件对象指针。
 * @param idx 目标行下标（越界或未变化时空操作）。
 * @return 无。
 */
static void _dropdown_key_hover_to(we_dropdown_obj_t *d, int16_t idx)
{
    int16_t old = d->hover_idx;
    int32_t row_top;

    if (idx < 0 || idx >= (int16_t)d->option_cnt || idx == old)
        return;
    d->hover_idx = idx;
    if (old >= 0)
        _dropdown_invalidate_row(d, old);
    _dropdown_invalidate_row(d, idx);

    /* 高亮行滚动跟随：目标行不完整可见时硬滚到完整露出 */
    _dropdown_stop_rebound(d);
    row_top = (int32_t)idx * (int32_t)d->item_h;
    if (row_top < d->sc.pos)
        _dropdown_scroll_to(d, row_top);
    else if (row_top + (int32_t)d->item_h > d->sc.pos + (int32_t)d->overlay.h)
        _dropdown_scroll_to(d, row_top + (int32_t)d->item_h - (int32_t)d->overlay.h);
}

/**
 * @brief 模态键通道处理：展开列表的键控导航。
 * @param d 控件对象指针。
 * @param key 语义键值（仅按下沿）。
 * @return 恒为 1（模态弹层吞掉全部按键）。
 * @note 上/下（或前/后、左/右）移动高亮并跳过禁用项，首键从当前选中项
 *       落位不位移；OK 选中高亮行（与触摸同一选择路径）后收起；
 *       BACK 直接收起不选择。
 */
static uint8_t _dropdown_popup_key_cb(we_dropdown_obj_t *d, uint8_t key)
{
    int16_t idx;
    int16_t step;

    switch (key)
    {
    case WE_KEY_UP:
    case WE_KEY_LEFT:
    case WE_KEY_PREV:
        step = -1;
        break;
    case WE_KEY_DOWN:
    case WE_KEY_RIGHT:
    case WE_KEY_NEXT:
        step = 1;
        break;
    case WE_KEY_OK:
        (void)_dropdown_pick(d, d->hover_idx);
        we_dropdown_close(d);
        return 1U;
    case WE_KEY_BACK:
        we_dropdown_close(d);
        return 1U;
    default:
        return 1U;
    }

    idx = d->hover_idx;
    if (idx < 0)
    {
        /* 首键落位：从当前选中项（无选中则首项）亮起，不位移 */
        idx = (d->selected_idx >= 0 && d->selected_idx < (int16_t)d->option_cnt)
                  ? d->selected_idx
                  : 0;
        if (!d->options[idx].disabled)
        {
            _dropdown_key_hover_to(d, idx);
            return 1U;
        }
    }
    do
    {
        idx = (int16_t)(idx + step);
    } while (idx >= 0 && idx < (int16_t)d->option_cnt && d->options[idx].disabled);
    if (idx >= 0 && idx < (int16_t)d->option_cnt)
        _dropdown_key_hover_to(d, idx); /* 到边界无可用项则原地不动 */
    return 1U;
}

/**
 * @brief 闭合态主框的按键/焦点处理：OK 按下沿按压、松开沿展开。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知。
 * @return 非 0 表示已消费。
 * @note 展开后的按键全部走模态键通道（_dropdown_popup_key_cb），
 *       不经过这里。
 */
static uint8_t _dropdown_key_cb(void *ptr, uint8_t key_evt)
{
    we_dropdown_obj_t *d = (we_dropdown_obj_t *)ptr;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
        return (d->enabled != 0U && d->options != NULL && d->option_cnt > 0U) ? 1U : 0U;
    case WE_KEY_EVT_DEFOCUS:
        return 1U;
    case WE_KEY_OK: /* 按下沿：主框进入按压态 */
        _dropdown_set_pressed(d, 1U);
        return 1U;
    case WE_KEY_EVT_OK_RELEASE: /* 松开沿：回弹并展开列表 */
        _dropdown_set_pressed(d, 0U);
        we_dropdown_open(d);
        return 1U;
    case WE_KEY_EVT_FLASH_END: /* 取消：仅回弹不展开 */
        _dropdown_set_pressed(d, 0U);
        return 1U;
    default:
        return 0U;
    }
}
#endif /* WE_CFG_ENABLE_KEY_INPUT && WE_DROPDOWN_USE_KEY */

/**
 * @brief 展开下拉列表：内嵌弹层对象挂到顶层链并声明模态。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_dropdown_open(we_dropdown_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL || obj->opened || !obj->enabled)
        return;
    if (obj->options == NULL || obj->option_cnt == 0U)
        return;

    obj->hover_idx = -1;
    we_scroll_reset(&obj->sc);

    /* 展开列表 = 顶层内嵌弹层对象 + 模态：命中/绘制/按键全部走统一对象派发，
     * 键按下沿以裸键值、松开沿以 键值|RELEASE_FLAG 直送 overlay 的 event_cb。 */
    _dropdown_layout_overlay(obj);
    we_obj_attach_to_top(obj->base.lcd, &obj->overlay);
    we_modal_open(obj->base.lcd, &obj->overlay);

    /* 滚动到能看到当前选中项的位置：让选中项尽量靠近可视区底部，
     * 再由 _dropdown_scroll_to 夹紧到 [0, max_scroll]。 */
    if (obj->selected_idx > 0)
    {
        int32_t want = (int32_t)(obj->selected_idx + 1) * (int32_t)obj->item_h - (int32_t)obj->overlay.h;
        if (want > 0)
            _dropdown_scroll_to(obj, want);
    }

    obj->opened = 1U;
    /* 展开即让滚动条短暂可见，提示"此处可滚动"，
     * 随后由中央动画引擎驱动的淡出动画自动隐藏（wake 内挂链）。 */
    obj->sb_alpha = 0U;
    obj->sb_idle_ms = 0U;
    _dropdown_sb_wake(obj);
    we_obj_invalidate((we_obj_t *)obj); /* 重绘主框箭头方向 */
}

/**
 * @brief 收起下拉列表。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_dropdown_close(we_dropdown_obj_t *obj)
{
    if (obj == NULL || !obj->opened)
        return;

    obj->opened = 0U;
    obj->hover_idx = -1;
    obj->sc.dragging = 0U;
    obj->sb_alpha = 0U;   /* 复位淡出状态，下次展开重新计时 */
    obj->sb_idle_ms = 0U;
    we_anim_stop(obj->base.lcd, &obj->sb_anim); /* 摘除淡出动画 */
    _dropdown_stop_rebound(obj);                /* 摘除回弹动画 */
    we_modal_close(obj->base.lcd, &obj->overlay);
    we_obj_invalidate(&obj->overlay); /* 摘链前标脏弹层旧区（残影擦除） */
    we_obj_detach(&obj->overlay);
    we_obj_invalidate((we_obj_t *)obj); /* 重绘主框箭头方向 */
}

/**
 * @brief 切换展开/收起状态。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_dropdown_toggle(we_dropdown_obj_t *obj)
{
    if (obj == NULL)
        return;
    if (obj->opened)
        we_dropdown_close(obj);
    else
        we_dropdown_open(obj);
}

/**
 * @brief 控件移动回调：未展开走通用 bbox 标脏；已展开同时处理 popup 区域。
 * @param ptr 控件对象指针。
 * @param x 新左上角 X。
 * @param y 新左上角 Y。
 * @return 无。
 */
static void _dropdown_set_pos_cb(void *ptr, int16_t x, int16_t y)
{
    we_dropdown_obj_t *d = (we_dropdown_obj_t *)ptr;

    if (d == NULL || d->base.lcd == NULL)
        return;
    if (d->base.x == x && d->base.y == y)
        return;

    we_obj_invalidate((we_obj_t *)d); /* 旧主框 */
    d->base.x = x;
    d->base.y = y;
    we_obj_invalidate((we_obj_t *)d); /* 新主框 */

    /* 已展开时，重算弹层区域并同步 overlay 几何（旧/新区域各标脏一次） */
    if (d->opened)
    {
        we_obj_invalidate(&d->overlay);
        _dropdown_layout_overlay(d);
        we_obj_invalidate(&d->overlay);
    }
}

/**
 * @brief 删除回调：展开态先收起列表（we_obj_delete 与父容器递归删除都经过这里）。
 * @param ptr 控件对象指针。
 * @return 无。
 * @note overlay 是另挂在顶层链上的伴生对象，内核只回收被删对象本身的引用，
 *       解除模态与摘 overlay 必须由本回调完成；两个动画节点的 owner 是本控件，
 *       由内核回收。
 */
static void _dropdown_delete_cb(void *ptr)
{
    we_dropdown_obj_t *d = (we_dropdown_obj_t *)ptr;

    if (d->opened)
        we_dropdown_close(d); /* 解除模态 + 摘 overlay + 标脏弹层区 */
}

static const we_class_t _dropdown_class = {
    .draw_cb = _dropdown_draw_cb,
    .event_cb = _dropdown_event_cb,
    .set_pos_cb = _dropdown_set_pos_cb,
    .delete_cb = _dropdown_delete_cb,
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_DROPDOWN_USE_KEY == 1)
    .class_flags = WE_CLASS_FLAG_FOCUSABLE, /* 键/焦点走统一 event_cb 通道 */
#endif
};

/**
 * @brief 初始化下拉控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 上下文。
 * @param x 左上 X。
 * @param y 左上 Y。
 * @param w 主框宽度。
 * @param h 主框高度（同时作为默认列表项高度）。
 * @param font 字体资源。
 * @return 无。
 */
void we_dropdown_obj_init(we_dropdown_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, int16_t w, int16_t h,
                          const unsigned char *font)
{
    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_dropdown_class;
    obj->base.next = NULL;

    obj->options = NULL;
    obj->option_cnt = 0U;
    obj->selected_idx = -1;
    obj->hover_idx = -1;
    obj->opened = 0U;
    obj->overlay.next = NULL;
    obj->overlay.parent = NULL;
    obj->overlay.class_p = &_dd_overlay_class;
    obj->overlay.lcd = lcd;
    obj->overlay.x = 0;
    obj->overlay.y = 0;
    obj->overlay.w = 0;
    obj->overlay.h = 0;
    obj->pressed = 0U;
    obj->enabled = 1U;
    obj->max_visible_items = WE_DROPDOWN_DEF_MAX_VISIBLE;
    obj->item_h = (uint16_t)h;
    obj->radius = (uint16_t)(h / 4);
    if (obj->radius < 4U)
        obj->radius = 4U;
    obj->font = font;
    obj->changed_cb = NULL;
    we_scroll_reset(&obj->sc);
    obj->sb_alpha = 0U;
    obj->sb_idle_ms = 0U;
    obj->sb_anim.next = NULL;
    obj->sb_anim.step_cb = NULL;
    obj->sb_anim.owner = NULL;
    obj->rb_anim.next = NULL;
    obj->rb_anim.step_cb = NULL;
    obj->rb_anim.owner = NULL;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 绑定选项数组（仅保存指针）；展开中先收起列表。
 * @param obj 控件对象指针。
 * @param options 选项数组。
 * @param option_cnt 选项个数。
 * @return 无。
 */
void we_dropdown_set_options(we_dropdown_obj_t *obj,
                             const we_dropdown_option_t *options,
                             uint16_t option_cnt)
{
    if (obj == NULL)
        return;
    we_dropdown_close(obj); /* 列表几何与行内容都随选项变化 */
    obj->options = options;
    obj->option_cnt = option_cnt;
    if (obj->selected_idx >= (int16_t)option_cnt)
        obj->selected_idx = -1;
    obj->sc.pos = 0;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置选中项并刷新主框（展开中同步刷新新旧两行的强调色）。
 * @param obj 控件对象指针。
 * @param index 选中项索引。
 * @return 无。
 */
void we_dropdown_set_selected(we_dropdown_obj_t *obj, int16_t index)
{
    int16_t old;

    if (obj == NULL || obj->options == NULL)
        return;
    if (index < 0 || index >= (int16_t)obj->option_cnt)
        return;
    if (obj->options[index].disabled)
        return;
    if (obj->selected_idx == index)
        return;
    old = obj->selected_idx;
    obj->selected_idx = index;
    we_obj_invalidate((we_obj_t *)obj);
    if (obj->opened)
    {
        _dropdown_invalidate_row(obj, old);
        _dropdown_invalidate_row(obj, index);
    }
}

/**
 * @brief 获取选中项索引。
 * @param obj 控件对象指针。
 * @return 选中索引，未选为 -1。
 */
int16_t we_dropdown_get_selected(const we_dropdown_obj_t *obj)
{
    return (obj != NULL) ? obj->selected_idx : -1;
}

/**
 * @brief 获取选中项关联值。
 * @param obj 控件对象指针。
 * @return value，未选为 0。
 */
int32_t we_dropdown_get_value(const we_dropdown_obj_t *obj)
{
    if (obj == NULL || obj->options == NULL ||
        obj->selected_idx < 0 || obj->selected_idx >= (int16_t)obj->option_cnt)
        return 0;
    return obj->options[obj->selected_idx].value;
}

/**
 * @brief 设置选中改变回调。
 * @param obj 控件对象指针。
 * @param cb 回调。
 * @return 无。
 */
void we_dropdown_set_changed_cb(we_dropdown_obj_t *obj, we_dropdown_changed_cb_t cb)
{
    if (obj != NULL)
        obj->changed_cb = cb;
}

/**
 * @brief 设置 popup 最多可见选项数（展开中先收起列表）。
 * @param obj 控件对象指针。
 * @param count 可见项数量（至少 1）。
 * @return 无。
 */
void we_dropdown_set_max_visible_items(we_dropdown_obj_t *obj, uint8_t count)
{
    if (obj == NULL || count == 0U)
        return;
    we_dropdown_close(obj);
    obj->max_visible_items = count;
}

/**
 * @brief 设置列表项高度（展开中先收起列表）。
 * @param obj 控件对象指针。
 * @param item_h 单项高度（像素）。
 * @return 无。
 */
void we_dropdown_set_item_height(we_dropdown_obj_t *obj, uint16_t item_h)
{
    if (obj == NULL || item_h == 0U)
        return;
    we_dropdown_close(obj);
    obj->item_h = item_h;
}

/**
 * @brief 启用/禁用：禁用后不响应触摸与按键、拒绝聚焦，文字改用禁用色；
 *        展开中禁用会先收起列表。
 * @param obj 控件对象指针。
 * @param enabled 非 0 启用，0 禁用。
 * @return 无。
 */
void we_dropdown_set_enabled(we_dropdown_obj_t *obj, uint8_t enabled)
{
    uint8_t en = (enabled != 0U) ? 1U : 0U;

    if (obj == NULL || obj->enabled == en)
        return;
    if (!en)
    {
        we_dropdown_close(obj);
        obj->pressed = 0U;
    }
    obj->enabled = en;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除下拉控件（展开态由删除回调先收起列表）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_dropdown_obj_delete(we_dropdown_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}
