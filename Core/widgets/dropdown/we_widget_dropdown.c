#include "we_widget_dropdown.h"
#include "we_font_text.h"
#include "we_render.h"

/* 内置配色（通过 RGB888TODEV 兼容 RGB565 / RGB888 两种色深）。 */
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

/* 滚动条几何与透明度（无轨道，仅半透明胶囊滑块）。 */
#define _DD_SB_WIDTH        5   /* 滑块宽度（像素）；圆角=宽度/2，需≥4 两头半圆才明显 */
#define _DD_SB_MARGIN       3   /* 滑块距 popup 右内边距（像素） */
#define _DD_SB_THUMB_OPA    150 /* 滑块透明度（半透明） */

/* 弹出列表与主框之间的间隙（像素）。 */
#define _DD_POPUP_GAP       3

/**
 * @brief 计算 popup 区域：优先向下展开，下方不足则向上，均不足取空间更大方向并限高。
 * @param d 下拉控件指针。
 * @param out 输出区域（屏幕绝对坐标）。
 * @return 无。
 */
static void _dropdown_calc_popup_area(we_dropdown_obj_t *d, we_area_t *out)
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

    out->x0 = d->base.x;
    out->y0 = py;
    out->x1 = (int16_t)(d->base.x + d->base.w - 1);
    out->y1 = (int16_t)(py + ph - 1);
}

/**
 * @brief 在指定裁剪矩形内垂直居中绘制一行文本（超出裁剪区自动剪裁）。
 * @param lcd GUI 上下文。
 * @param font 字体资源。
 * @param text 文本。
 * @param color 文本颜色。
 * @param box_x 文本框左上 X（文本左对齐起点 = box_x + pad）。
 * @param box_y 文本框左上 Y。
 * @param box_w 文本框宽度。
 * @param box_h 文本框高度。
 * @param pad 左内边距。
 * @param opacity 透明度。
 * @return 无。
 */
static void _dropdown_draw_text_clipped(we_lcd_t *lcd, const unsigned char *font,
                                        const char *text, colour_t color,
                                        int16_t box_x, int16_t box_y,
                                        int16_t box_w, int16_t box_h,
                                        int16_t pad, uint8_t opacity)
{
    we_area_t old_pfb_area;
    uint16_t old_y_start, old_y_end;
    colour_t *old_gram;
    int8_t y_top, y_bot;
    int16_t txt_x, txt_y;
    int16_t cx0, cy0, cx1, cy1;

    if (text == NULL || font == NULL)
        return;

    we_get_text_bbox(font, text, &y_top, &y_bot);
    txt_x = (int16_t)(box_x + pad);
    txt_y = (int16_t)(box_y + box_h / 2 - (y_top + y_bot) / 2);

    old_pfb_area = lcd->pfb_area;
    old_y_start = lcd->pfb_y_start;
    old_y_end = lcd->pfb_y_end;
    old_gram = lcd->pfb_gram;

    cx0 = WE_MAX(old_pfb_area.x0, box_x);
    cy0 = WE_MAX((int16_t)old_y_start, box_y);
    cx1 = WE_MIN(old_pfb_area.x1, (int16_t)(box_x + box_w - 1));
    cy1 = WE_MIN((int16_t)old_y_end, (int16_t)(box_y + box_h - 1));

    if (cx0 <= cx1 && cy0 <= cy1)
    {
        lcd->pfb_area.x0 = cx0;
        lcd->pfb_area.x1 = cx1;
        lcd->pfb_y_start = (uint16_t)cy0;
        lcd->pfb_y_end = (uint16_t)cy1;
        lcd->pfb_gram = old_gram + (cy0 - (int16_t)old_y_start) * lcd->pfb_width
                                 + (cx0 - old_pfb_area.x0);

        we_draw_string(lcd, txt_x, txt_y, font, text, color, opacity);
    }

    lcd->pfb_area = old_pfb_area;
    lcd->pfb_y_start = old_y_start;
    lcd->pfb_y_end = old_y_end;
    lcd->pfb_gram = old_gram;
}

/**
 * @brief 在主框右侧绘制实心三角箭头（展开朝上▲，收起朝下▼）。
 *
 * 采用解析覆盖率：在三角形包围盒内逐像素做 4x4 子采样，统计落在三角形
 * 内的子样本数作为该像素覆盖率（0~16），再映射成 alpha 与背景混合。
 * 纯整数 edge-function 判定，无 sqrt/浮点，尖角是真正收敛的数学尖点，
 * 边缘为连续灰阶抗锯齿。包围盒约 11x7 像素，开销极小。
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

    /* 三角形三个顶点（屏幕绝对坐标）。子像素精度统一放大 4 倍。 */
    int32_t ax, ay, bx, by, tx, ty; /* 左角 / 右角 / 尖点 */
    int16_t bb_x0, bb_y0, bb_x1, bb_y1;
    int16_t px, py;

    if (d->opened)
    {
        /* 朝上▲：底边两角在下，尖点在上 */
        ax = (int32_t)(cx - hw) * 4; ay = (int32_t)(cy + vh) * 4;
        bx = (int32_t)(cx + hw) * 4; by = (int32_t)(cy + vh) * 4;
        tx = (int32_t)cx * 4;        ty = (int32_t)(cy - vh) * 4;
    }
    else
    {
        /* 朝下▼：顶边两角在上，尖点在下 */
        ax = (int32_t)(cx - hw) * 4; ay = (int32_t)(cy - vh) * 4;
        bx = (int32_t)(cx + hw) * 4; by = (int32_t)(cy - vh) * 4;
        tx = (int32_t)cx * 4;        ty = (int32_t)(cy + vh) * 4;
    }

    bb_x0 = (int16_t)(cx - hw);
    bb_x1 = (int16_t)(cx + hw);
    bb_y0 = (int16_t)(cy - vh);
    bb_y1 = (int16_t)(cy + vh);

    for (py = bb_y0; py <= bb_y1; py++)
    {
        for (px = bb_x0; px <= bb_x1; px++)
        {
            uint8_t inside = 0U;
            int16_t sx, sy;

            /* 4x4 子采样：子样本中心 = 像素左上*4 + (1,3,5,7)/2 → 取 +1,+3,+5,+7 的一半。
             * 这里直接用 (px*4 + 1 + 2*k) 表示第 k 个子样本中心（k=0..3）。 */
            for (sy = 0; sy < 4; sy++)
            {
                int32_t spy = (int32_t)py * 4 + 1 + 2 * sy;
                for (sx = 0; sx < 4; sx++)
                {
                    int32_t spx = (int32_t)px * 4 + 1 + 2 * sx;
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
                uint8_t a = (uint8_t)(((uint16_t)inside * 255U) / 16U);
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

    if (d->selected_idx >= 0 && d->selected_idx < (int16_t)d->option_cnt &&
        d->options != NULL)
    {
        txt = d->options[d->selected_idx].text;
    }
    if (txt != NULL)
    {
        colour_t tc = d->enabled ? _DD_COL_TEXT : _DD_COL_TEXT_DIS;
        _dropdown_draw_text_clipped(lcd, d->font, txt, tc,
                                    d->base.x, d->base.y,
                                    (int16_t)(d->base.w - 22), d->base.h, 8, 255U);
    }

    _dropdown_draw_arrow(d);
}

/**
 * @brief 当前可见项数量（受 max_visible_items 与 popup 实际高度共同约束）。
 * @param d 下拉控件指针。
 * @return 可见项数量。
 */
static uint16_t _dropdown_visible_count(we_dropdown_obj_t *d)
{
    we_area_t *a = &d->base.lcd->popup_layer.area;
    uint16_t fit = (uint16_t)((a->y1 - a->y0 + 1) / (int16_t)d->item_h);
    uint16_t rest = (uint16_t)(d->option_cnt - d->first_visible_idx);
    if (fit > rest)
        fit = rest;
    if (fit > d->max_visible_items)
        fit = d->max_visible_items;
    return fit;
}

/**
 * @brief 在 popup 圆角矩形 mask 内填充某一选项行的高亮背景。
 *
 * 直接 we_fill_rect 会画出直角，首/末项会溢出 popup 的圆角；这里对行内
 * 每个像素取 popup 整体圆角矩形的覆盖率，使高亮边缘自然跟随圆角。
 * @param d 下拉控件指针。
 * @param a popup 区域（屏幕绝对坐标）。
 * @param iy 行顶部 Y。
 * @param color 高亮色。
 * @return 无。
 */
static void _dropdown_fill_item_rounded(we_dropdown_obj_t *d, const we_area_t *a,
                                        int16_t iy, colour_t color)
{
    we_lcd_t *lcd = d->base.lcd;
    int16_t pw = (int16_t)(a->x1 - a->x0 + 1);
    int16_t ph = (int16_t)(a->y1 - a->y0 + 1);
    uint16_t r = d->radius;
    int16_t row_y1 = (int16_t)(iy + (int16_t)d->item_h - 1);
    int16_t px, py;

    if (r > (uint16_t)pw / 2U) r = (uint16_t)(pw / 2U);
    if (r > (uint16_t)ph / 2U) r = (uint16_t)(ph / 2U);

    /* 仅当本行接近 popup 上/下边缘时才需要 mask；中间行整体是直边，
     * 但统一走 mask 也只是多算覆盖率，逻辑简单、开销可接受。 */
    for (py = iy; py <= row_y1; py++)
    {
        for (px = a->x0; px <= a->x1; px++)
        {
            uint8_t m = we_mask_round_rect_alpha(a->x0, a->y0, (uint16_t)pw,
                                                 (uint16_t)ph, r, px, py);
            if (m == 0U)
                continue;
            we_draw_pixel(lcd, px, py, color, m);
        }
    }
}

/**
 * @brief 在 popup 右侧绘制半透明滚动条（轨道 + 滑块）。
 *
 * 仅当选项总数超过可见数时显示。滑块高度按“可见数/总数”比例，
 * 位置按 first_visible_idx 在可滚动范围内的占比。纯绘制，不参与交互。
 * @param d 下拉控件指针。
 * @param a popup 区域（屏幕绝对坐标）。
 * @param vis 当前可见项数。
 * @return 无。
 */
static void _dropdown_draw_scrollbar(we_dropdown_obj_t *d, const we_area_t *a, uint16_t vis)
{
    we_lcd_t *lcd = d->base.lcd;
    int16_t ph = (int16_t)(a->y1 - a->y0 + 1);
    int16_t track_x;
    int16_t track_y0;
    int16_t track_h;
    int16_t thumb_h;
    int16_t thumb_y;
    uint16_t max_first;

    if (d->option_cnt <= vis || vis == 0U)
        return; /* 无需滚动 */

    /* 轨道：上下各留 radius 像素，避免压到圆角；宽度固定。 */
    track_x = (int16_t)(a->x1 - _DD_SB_MARGIN - _DD_SB_WIDTH + 1);
    track_y0 = (int16_t)(a->y0 + (int16_t)d->radius);
    track_h = (int16_t)(ph - 2 * (int16_t)d->radius);
    if (track_h < (int16_t)d->item_h)
        track_h = ph; /* 圆角过大时退化为整高轨道 */

    /* 滑块高度 = 轨道高 * 可见数 / 总数，下限保证可见。 */
    thumb_h = (int16_t)((int32_t)track_h * vis / d->option_cnt);
    if (thumb_h < 8)
        thumb_h = 8;
    if (thumb_h > track_h)
        thumb_h = track_h;

    /* 滑块位置 = 在 [0, track_h - thumb_h] 内按滚动进度插值。 */
    max_first = (uint16_t)(d->option_cnt - vis);
    thumb_y = track_y0;
    if (max_first > 0U)
    {
        thumb_y = (int16_t)(track_y0 +
                  (int32_t)(track_h - thumb_h) * d->first_visible_idx / max_first);
    }

    /* 仅绘制半透明胶囊滑块，不画轨道；圆角=半宽 → 两头自然收成半圆。 */
    we_draw_round_rect_analytic_fill(lcd, track_x, thumb_y, _DD_SB_WIDTH,
                                     (uint16_t)thumb_h, _DD_SB_WIDTH / 2U,
                                     _DD_COL_SCROLLBAR, _DD_SB_THUMB_OPA);
}

/**
 * @brief popup 绘制回调：列表背景圆角矩形 + 可见项文本（含高亮/禁用）。
 * @param owner 控件对象指针（由 popup_layer 透传）。
 * @return 无。
 */
static void _dropdown_popup_draw(void *owner)
{
    we_dropdown_obj_t *d = (we_dropdown_obj_t *)owner;
    we_lcd_t *lcd = d->base.lcd;
    we_area_t *a = &lcd->popup_layer.area;
    int16_t pw = (int16_t)(a->x1 - a->x0 + 1);
    int16_t ph = (int16_t)(a->y1 - a->y0 + 1);
    uint16_t vis = _dropdown_visible_count(d);
    uint16_t i;

    /* 列表整体背景：用比主框更亮的悬浮色，圆角与主框一致。 */
    we_draw_round_rect_analytic_fill(lcd, a->x0, a->y0, (uint16_t)pw, (uint16_t)ph,
                                     d->radius, _DD_COL_POPUP_BG, 255U);

    for (i = 0; i < vis; i++)
    {
        uint16_t idx = (uint16_t)(d->first_visible_idx + i);
        int16_t iy = (int16_t)(a->y0 + i * d->item_h);
        const we_dropdown_option_t *op = &d->options[idx];
        colour_t tc;

        if ((int16_t)idx == d->hover_idx)
        {
            /* 按下高亮：整行铺选中背景色，并跟随 popup 圆角裁剪。 */
            _dropdown_fill_item_rounded(d, a, iy, _DD_COL_SEL_BG);
            tc = _DD_COL_SEL_TEXT;
        }
        else if ((int16_t)idx == d->selected_idx)
        {
            tc = _DD_COL_SEL_BG; /* 当前选中项用强调色文本标识 */
        }
        else
        {
            tc = op->disabled ? _DD_COL_TEXT_DIS : _DD_COL_TEXT;
        }
        if (op->disabled && (int16_t)idx != d->hover_idx)
            tc = _DD_COL_TEXT_DIS;

        _dropdown_draw_text_clipped(lcd, d->font, op->text, tc,
                                    a->x0, iy, pw, (int16_t)d->item_h, 8, 255U);
    }

    /* 选项超出可见范围时，叠加半透明滚动条。 */
    _dropdown_draw_scrollbar(d, a, vis);
}

/**
 * @brief 主框事件回调：点击主框切换展开/收起。
 * @param ptr 控件对象指针。
 * @param event 输入事件。
 * @param data 输入数据。
 * @return 1 表示消费事件。
 */
static uint8_t _dropdown_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_dropdown_obj_t *d = (we_dropdown_obj_t *)ptr;
    (void)data;

    if (!d->enabled)
        return 1U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        if (!d->pressed)
        {
            d->pressed = 1U;
            we_obj_invalidate((we_obj_t *)d);
        }
        break;
    case WE_EVENT_RELEASED:
        if (d->pressed)
        {
            d->pressed = 0U;
            we_obj_invalidate((we_obj_t *)d);
        }
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
 * @brief 命中测试：返回点 (px,py) 落在 popup 中的选项索引，未命中返回 -1。
 * @param d 下拉控件指针。
 * @param px 触摸 X。
 * @param py 触摸 Y。
 * @return 选项索引或 -1。
 */
static int16_t _dropdown_popup_hit(we_dropdown_obj_t *d, int16_t px, int16_t py)
{
    we_area_t *a = &d->base.lcd->popup_layer.area;
    uint16_t vis = _dropdown_visible_count(d);
    int16_t rel;
    uint16_t row;

    if (px < a->x0 || px > a->x1 || py < a->y0 || py > a->y1)
        return -1;

    rel = (int16_t)(py - a->y0);
    row = (uint16_t)(rel / (int16_t)d->item_h);
    if (row >= vis)
        return -1;
    return (int16_t)(d->first_visible_idx + row);
}

/**
 * @brief 滚动列表使顶部可见项为 new_first（带边界夹紧并标脏）。
 * @param d 下拉控件指针。
 * @param new_first 目标顶部项索引。
 * @return 无。
 */
static void _dropdown_scroll_to(we_dropdown_obj_t *d, int16_t new_first)
{
    we_area_t *a = &d->base.lcd->popup_layer.area;
    uint16_t fit = (uint16_t)((a->y1 - a->y0 + 1) / (int16_t)d->item_h);
    int16_t max_first;

    if (fit > d->max_visible_items)
        fit = d->max_visible_items;
    max_first = (int16_t)d->option_cnt - (int16_t)fit;
    if (max_first < 0)
        max_first = 0;
    if (new_first < 0)
        new_first = 0;
    if (new_first > max_first)
        new_first = max_first;

    if ((uint16_t)new_first != d->first_visible_idx)
    {
        d->first_visible_idx = (uint16_t)new_first;
        we_popup_layer_invalidate(d->base.lcd);
    }
}

/**
 * @brief popup 事件回调：拖拽滚动 / 选项选择 / 点击外部关闭。
 * @param owner 控件对象指针。
 * @param event 输入事件。
 * @param data 输入数据。
 * @return 1 表示消费事件（阻止穿透到下层控件）。
 */
static uint8_t _dropdown_popup_event(void *owner, we_event_t event, we_indev_data_t *data)
{
    we_dropdown_obj_t *d = (we_dropdown_obj_t *)owner;
    we_area_t *a = &d->base.lcd->popup_layer.area;
    int16_t hit;
    uint8_t inside = (data->x >= a->x0 && data->x <= a->x1 &&
                      data->y >= a->y0 && data->y <= a->y1) ? 1U : 0U;

    if (event == WE_EVENT_PRESSED)
    {
        d->drag_start_y = data->y;
        d->drag_start_first = d->first_visible_idx;
        d->dragging = 0U;
        if (inside)
        {
            hit = _dropdown_popup_hit(d, data->x, data->y);
            if (hit != d->hover_idx)
            {
                d->hover_idx = hit;
                we_popup_layer_invalidate(d->base.lcd);
            }
        }
        return 1U;
    }
    else if (event == WE_EVENT_STAY)
    {
        int16_t dy = (int16_t)(data->y - d->drag_start_y);
        int16_t adyy = (dy >= 0) ? dy : (int16_t)(-dy);
        if (adyy >= (int16_t)(d->item_h / 2))
            d->dragging = 1U;

        if (d->dragging)
        {
            int16_t steps = (int16_t)(dy / (int16_t)d->item_h);
            if (d->hover_idx != -1)
            {
                d->hover_idx = -1; /* 拖拽中取消按下高亮 */
                we_popup_layer_invalidate(d->base.lcd);
            }
            _dropdown_scroll_to(d, (int16_t)((int16_t)d->drag_start_first - steps));
        }
        return 1U;
    }
    else if (event == WE_EVENT_RELEASED)
    {
        int16_t sel = d->hover_idx;
        uint8_t was_drag = d->dragging;
        d->hover_idx = -1;
        d->dragging = 0U;

        if (was_drag)
        {
            /* 拖拽滚动结束：保持展开，不选择、不关闭，仅清掉按下高亮。 */
            we_popup_layer_invalidate(d->base.lcd);
            return 1U;
        }
        if (!inside)
        {
            /* 点击外部：关闭，消费事件避免穿透到下层控件。 */
            we_dropdown_close(d);
            return 1U;
        }
        if (sel >= 0 && sel < (int16_t)d->option_cnt && !d->options[sel].disabled)
        {
            we_dropdown_set_selected(d, sel);
            if (d->changed_cb != NULL)
                d->changed_cb(d, sel, d->options[sel].value);
            we_dropdown_close(d);
        }
        else
        {
            we_popup_layer_invalidate(d->base.lcd); /* 禁用项：取消高亮重绘 */
        }
        return 1U;
    }

    return 1U;
}

/**
 * @brief 展开下拉列表，占用唯一 popup slot。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_dropdown_open(we_dropdown_obj_t *obj)
{
    we_area_t area;

    if (obj == NULL || obj->base.lcd == NULL || obj->opened || !obj->enabled)
        return;
    if (obj->options == NULL || obj->option_cnt == 0U)
        return;

    /* 滚动到能看到当前选中项的位置 */
    obj->first_visible_idx = 0U;
    if (obj->selected_idx >= obj->max_visible_items)
    {
        obj->first_visible_idx =
            (uint16_t)(obj->selected_idx - obj->max_visible_items + 1);
    }
    obj->hover_idx = -1;
    obj->dragging = 0U;

    _dropdown_calc_popup_area(obj, &area);
    we_popup_layer_open(obj->base.lcd, WE_POPUP_TYPE_DROPDOWN, obj, &area,
                        _dropdown_popup_draw, _dropdown_popup_event, NULL);

    obj->opened = 1U;
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
    obj->dragging = 0U;
    we_popup_layer_close(obj->base.lcd, obj);
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
    we_area_t new_area;

    if (d == NULL || d->base.lcd == NULL)
        return;
    if (d->base.x == x && d->base.y == y)
        return;

    /* 标脏旧主框 */
    we_obj_invalidate((we_obj_t *)d);

    d->base.x = x;
    d->base.y = y;

    /* 标脏新主框 */
    we_obj_invalidate((we_obj_t *)d);

    /* 已展开时，重新计算 popup area，由 set_area 负责标脏旧/新区域 */
    if (d->opened && we_popup_layer_is_owner(d->base.lcd, d))
    {
        _dropdown_calc_popup_area(d, &new_area);
        we_popup_layer_set_area(d->base.lcd, d, &new_area);
    }
}

static const we_class_t _dropdown_class = {
    .draw_cb = _dropdown_draw_cb,
    .event_cb = _dropdown_event_cb,
    .set_pos_cb = _dropdown_set_pos_cb,
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
    obj->first_visible_idx = 0U;
    obj->opened = 0U;
    obj->pressed = 0U;
    obj->enabled = 1U;
    obj->max_visible_items = WE_DROPDOWN_DEF_MAX_VISIBLE;
    obj->item_h = (uint16_t)h;
    obj->radius = (uint16_t)(h / 4);
    if (obj->radius < 4U)
        obj->radius = 4U;
    obj->font = font;
    obj->changed_cb = NULL;
    obj->drag_start_y = 0;
    obj->drag_start_first = 0U;
    obj->dragging = 0U;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 绑定选项数组（仅保存指针）。
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
    obj->options = options;
    obj->option_cnt = option_cnt;
    if (obj->selected_idx >= (int16_t)option_cnt)
        obj->selected_idx = -1;
    obj->first_visible_idx = 0U;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置选中项并刷新主框。
 * @param obj 控件对象指针。
 * @param index 选中项索引。
 * @return 无。
 */
void we_dropdown_set_selected(we_dropdown_obj_t *obj, int16_t index)
{
    if (obj == NULL || obj->options == NULL)
        return;
    if (index < 0 || index >= (int16_t)obj->option_cnt)
        return;
    if (obj->options[index].disabled)
        return;
    if (obj->selected_idx == index)
        return;
    obj->selected_idx = index;
    we_obj_invalidate((we_obj_t *)obj);
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
 * @brief 设置 popup 最多可见选项数。
 * @param obj 控件对象指针。
 * @param count 可见项数量（至少 1）。
 * @return 无。
 */
void we_dropdown_set_max_visible_items(we_dropdown_obj_t *obj, uint8_t count)
{
    if (obj == NULL || count == 0U)
        return;
    obj->max_visible_items = count;
}

/**
 * @brief 设置列表项高度。
 * @param obj 控件对象指针。
 * @param item_h 单项高度（像素）。
 * @return 无。
 */
void we_dropdown_set_item_height(we_dropdown_obj_t *obj, uint16_t item_h)
{
    if (obj == NULL || item_h == 0U)
        return;
    obj->item_h = item_h;
}

/**
 * @brief 删除下拉控件（展开时先关闭 popup，再摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_dropdown_obj_delete(we_dropdown_obj_t *obj)
{
    if (obj == NULL)
        return;
    if (obj->base.lcd != NULL && we_popup_layer_is_owner(obj->base.lcd, obj))
        we_popup_layer_close(obj->base.lcd, obj);
    obj->opened = 0U;
    we_obj_delete((we_obj_t *)obj);
}







