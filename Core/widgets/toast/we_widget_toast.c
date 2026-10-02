/**
 * @file  we_widget_toast.c
 * @brief 轻提示横幅控件（toast）：顶部滑入 → 停留 → 滑出的非模态提示
 *
 * 单个中央动画节点驱动三阶段状态机（ENTER/STAY/EXIT）：
 * Q8 进度 + we_ease_out_quad，每步同时更新 base.y 与 opacity，
 * 滑动步进把旧/新包围盒合并成一个 union 矩形单次标脏。
 * 非模态：class 的 event_cb 为 NULL，输入完全穿透；不声明模态。
 * 超宽文本尾部截断加 "..."（零拷贝：前缀经窗口右界收窄绘制）。
 * 文字排版（居中偏移 / 截断前缀宽）在 show、换字体、改边距时算一次，
 * 绘制回调不再测量文字。
 */

#include "we_widget_toast.h"
#include "wegui.h"
#include "components/we_motion.h"

/* 阶段状态机取值（存入 obj->state） */
enum
{
    WE_TOAST_ST_HIDDEN = 0, /* 隐藏：屏外停放，draw_cb 直接 return */
    WE_TOAST_ST_ENTER,      /* 滑入：from_y/from_opa → 停靠位/全显 */
    WE_TOAST_ST_STAY,       /* 停留：静止计时 duration_ms */
    WE_TOAST_ST_EXIT        /* 滑出：当前位 → 屏外/全透 */
};

/* --------------------------------------------------------------------------
 * 内部工具
 * -------------------------------------------------------------------------- */

/**
 * @brief 计算隐藏态停放 Y（整体移出屏幕顶部再留 4px 余量）。
 * @param obj 控件对象指针。
 * @return 隐藏态 Y 坐标（负值）。
 */
static __inline int16_t _toast_hidden_y(const we_toast_obj_t *obj)
{
    return (int16_t)(-(obj->base.h + 4));
}

/* 文字排版方式（存入 obj->fit_mode） */
enum
{
    _TOAST_FIT_CENTER = 0, /* 放得下：水平居中 */
    _TOAST_FIT_ELLIPSIS,   /* 超宽：前缀 + "..." */
    _TOAST_FIT_CLIP        /* 窄到放不下 "..."：左对齐硬裁剪 */
};

/**
 * @brief 求文本在给定像素宽内能容纳的最大前缀宽度（省略号截断用，
 *        mlabel 前缀量取思路的简化版：只要宽度、不要字节数）。
 * @param font 传入：字库指针。
 * @param text 传入：UTF-8 文本。
 * @param avail_w 传入：可用像素宽（已减去 "..." 宽度）。
 * @return 前缀实际像素宽（adv_w 逐字符累计）。
 * @note 只需前缀宽度即可确定 PFB 裁剪右界与省略号起绘点，无需拷贝
 *       字符串（零 malloc / 零栈缓冲）。遇 '\n' 停止（toast 单行语义）。
 */
static int32_t _toast_prefix_fit_w(const unsigned char *font, const char *text,
                                   int32_t avail_w)
{
    const char *p = text;
    int32_t width = 0;
    we_glyph_info_t info;

    for (;;)
    {
        uint16_t code = 0U;
        uint16_t adv;

        if (we_utf8_next(&p, &code) == 0U)
            break;
        if (code == (uint16_t)'\n')
            break;

        adv = we_font_get_glyph_info(font, code, &info) ? info.adv_w : 0U;
        if (width + (int32_t)adv > avail_w)
            break;
        width += (int32_t)adv;
    }
    return width;
}

/**
 * @brief 计算文字排版并缓存：竖直居中偏移、水平起点、截断方式与前缀宽。
 * @param obj 控件对象指针。
 * @return 无。
 * @note 依赖文本、字体、横幅宽高，三者变化时（show / set_font / set_margin）调用。
 */
static void _toast_layout(we_toast_obj_t *obj)
{
    uint16_t txt_w;
    int32_t avail_w;
    int8_t y_top;
    int8_t y_bottom;

    if (obj->text == NULL)
        return;

    txt_w = we_get_text_width(obj->font, obj->text);
    we_get_text_bbox(obj->font, obj->text, &y_top, &y_bottom);
    obj->txt_oy = (int16_t)(obj->base.h / 2 - (y_top + y_bottom) / 2); /* 按墨迹竖直居中 */
    avail_w = (int32_t)obj->base.w - 2 * WE_TOAST_TEXT_PAD;

    if ((int32_t)txt_w <= avail_w)
    {
        obj->fit_mode = _TOAST_FIT_CENTER;
        obj->txt_ox = (int16_t)((obj->base.w - (int16_t)txt_w) / 2);
        return;
    }

    obj->txt_ox = WE_TOAST_TEXT_PAD;
    {
        int32_t ell_w = (int32_t)we_get_text_width(obj->font, "...");

        if (avail_w > ell_w)
        {
            /* 尾部省略号截断：前缀最多占 (可用宽 - "..."宽) */
            obj->fit_mode = _TOAST_FIT_ELLIPSIS;
            obj->prefix_w = (int16_t)_toast_prefix_fit_w(obj->font, obj->text, avail_w - ell_w);
        }
        else
        {
            obj->fit_mode = _TOAST_FIT_CLIP;
        }
    }
}

/**
 * @brief 同步应用一步动画结果：更新 Y 与透明度，旧/新包围盒合并单次标脏。
 * @param obj 控件对象指针。
 * @param new_y 新的顶边 Y 坐标。
 * @param new_opa 新的不透明度。
 * @return 无。
 * @note 位移与透明度同帧变化。toast 的位置由控件手动管理（直接改
 *       base.y，绕开 we_obj_set_pos）：set_pos 的通用路径固定"旧 bbox
 *       一次 + 新 bbox 一次"两次提交，无法表达 union 语义。滑动步进
 *       x/w/h 不变、只有 y 平移几个像素，旧∪新是仅比单帧 bbox 高
 *       |dy| 的窄矩形——一次提交比两次省一个脏矩形合并器槽位
 *       （WE_CFG_DIRTY_MAX_NUM 有限，多控件同屏动画时槽位紧张）。
 *       toast 无父容器，屏幕绝对坐标经 we_obj_invalidate_area 的
 *       parent 链裁剪原样通过；屏外部分由脏区管理对屏幕求交裁掉。
 */
static void _toast_apply_frame(we_toast_obj_t *obj, int16_t new_y, uint8_t new_opa)
{
    int16_t uy0;
    int16_t uy1;

    if (obj->base.y == new_y && obj->opacity == new_opa)
        return;

    uy0 = WE_MIN(obj->base.y, new_y);
    uy1 = (int16_t)(WE_MAX(obj->base.y, new_y) + obj->base.h - 1);

    obj->base.y = new_y;
    obj->opacity = new_opa;

    we_obj_invalidate_area((we_obj_t *)obj, obj->base.x, uy0,
                           obj->base.w, (int16_t)(uy1 - uy0 + 1));
}

/**
 * @brief 中央动画引擎回调：推进 ENTER/STAY/EXIT 三阶段状态机。
 * @param owner 控件对象指针。
 * @param elapsed_ms 本调度周期经过的毫秒数。
 * @return 无。
 * @note EXIT 收尾后置 HIDDEN 并自行 we_anim_stop 摘链（空闲零开销）。
 */
static void _toast_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_toast_obj_t *obj = (we_toast_obj_t *)owner;
    uint16_t t;
    uint16_t eased;

    if (obj == NULL || obj->base.lcd == NULL)
        return;

    switch (obj->state)
    {
    case WE_TOAST_ST_ENTER:
    case WE_TOAST_ST_EXIT:
    {
        int16_t target_y;
        uint8_t target_opa;
        uint32_t acc = (uint32_t)obj->phase_acc + elapsed_ms;

        if (acc > WE_TOAST_ANIM_MS)
            acc = WE_TOAST_ANIM_MS;
        obj->phase_acc = (uint16_t)acc;

        if (obj->state == WE_TOAST_ST_ENTER)
        {
            target_y = obj->margin_top; /* 停靠位可 set_margin 调整 */
            target_opa = 255U;
        }
        else
        {
            target_y = _toast_hidden_y(obj);
            target_opa = 0U;
        }

        t = (uint16_t)((acc * 256U) / WE_TOAST_ANIM_MS);
        eased = we_ease_out_quad(t);
        _toast_apply_frame(obj,
                           (int16_t)we_lerp(obj->from_y, target_y, eased),
                           (uint8_t)we_lerp(obj->from_opa, target_opa, eased));

        if (obj->phase_acc >= WE_TOAST_ANIM_MS)
        {
            if (obj->state == WE_TOAST_ST_ENTER)
            {
                /* 滑入到位 → 进入停留计时 */
                _toast_apply_frame(obj, target_y, target_opa);
                obj->state = WE_TOAST_ST_STAY;
                obj->phase_acc = 0U;
            }
            else
            {
                /* 滑出完成 → 隐藏并摘链 */
                _toast_apply_frame(obj, target_y, 0U);
                obj->state = WE_TOAST_ST_HIDDEN;
                obj->phase_acc = 0U;
                we_anim_stop(obj->base.lcd, &obj->anim);
            }
        }
        break;
    }

    case WE_TOAST_ST_STAY:
    {
        uint32_t acc = (uint32_t)obj->phase_acc + elapsed_ms;

        if (acc >= obj->duration_ms)
        {
            /* 停留结束 → 从当前停靠位开始滑出 */
            obj->state = WE_TOAST_ST_EXIT;
            obj->phase_acc = 0U;
            obj->from_y = obj->base.y;
            obj->from_opa = obj->opacity;
        }
        else
        {
            obj->phase_acc = (uint16_t)acc; /* 静止阶段不重绘 */
        }
        break;
    }

    case WE_TOAST_ST_HIDDEN:
    default:
        we_anim_stop(obj->base.lcd, &obj->anim); /* 防御：隐藏态不应留在动画链上 */
        break;
    }
}

/* --------------------------------------------------------------------------
 * 绘制
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：圆角面板 + 水平/垂直居中文字；隐藏态直接返回。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note 内核已把窗口收窄到横幅矩形，文字像素画不出横幅之外。
 *       超宽时左对齐起绘，尾部截断并追加 "..."：前缀零拷贝绘制（临时把
 *       窗口右界收到前缀末端画整串，其后字形被裁掉），省略号在前缀末端补画。
 */
static void _toast_draw_cb(void *ptr)
{
    we_toast_obj_t *obj = (we_toast_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    int16_t txt_x;
    int16_t txt_y;

    if (obj->state == WE_TOAST_ST_HIDDEN || obj->opacity == 0U || obj->text == NULL)
        return;

#if (WE_CFG_MONO_STYLE == 1)
    /* 单色样式：黑底横幅 + 1px 白描边 */
    we_draw_round_rect_outline_fill(lcd, obj->base.x, obj->base.y,
                                    (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                    WE_TOAST_RADIUS, RGB888TODEV(255, 255, 255),
                                    RGB888TODEV(0, 0, 0), obj->opacity);
#else
    we_draw_round_rect_analytic_fill(lcd, obj->base.x, obj->base.y,
                                     (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                     WE_TOAST_RADIUS, obj->bg_color, obj->opacity);
#endif

    txt_x = (int16_t)(obj->base.x + obj->txt_ox);
    txt_y = (int16_t)(obj->base.y + obj->txt_oy);
    if (obj->fit_mode != _TOAST_FIT_ELLIPSIS)
    {
        we_draw_string(lcd, txt_x, txt_y, obj->font, obj->text, obj->text_color, obj->opacity);
        return;
    }

    if (obj->prefix_w > 0)
    {
        we_pfb_clip_t sv;

        we_pfb_clip_save(lcd, &sv);
        if (we_pfb_clip_to(lcd, &sv, sv.clip.x0, sv.clip.y0,
                           (int16_t)(txt_x + obj->prefix_w - 1), sv.clip.y1))
        {
            we_draw_string(lcd, txt_x, txt_y, obj->font, obj->text, obj->text_color, obj->opacity);
            we_pfb_clip_restore(lcd, &sv);
        }
    }
    we_draw_string(lcd, (int16_t)(txt_x + obj->prefix_w), txt_y,
                   obj->font, "...", obj->text_color, obj->opacity);
}
/* 非模态：event_cb 置 NULL，核心输入分发完全跳过本控件（横幅不挡输入） */
static const we_class_t _toast_class = {
    .draw_cb    = _toast_draw_cb,
    .event_cb   = NULL,
    .set_pos_cb = NULL
};

/* --------------------------------------------------------------------------
 * 生命周期与 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化轻提示横幅并挂载到 LCD 对象链表（初始隐藏、停在屏外）。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @return 无。
 */
void we_toast_obj_init(we_toast_obj_t *obj, we_lcd_t *lcd, const unsigned char *font)
{
    uint16_t line_h;

    if (obj == NULL || lcd == NULL || font == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.class_p = &_toast_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    obj->margin_top = WE_TOAST_DOCK_Y;
    obj->margin_side = WE_TOAST_MARGIN_X;

    obj->font = font;
    line_h = we_font_get_line_height(obj->font);
    obj->base.w = (int16_t)((int16_t)lcd->width - 2 * obj->margin_side);
    obj->base.h = (int16_t)(line_h + 2 * WE_TOAST_PAD_Y);
    obj->base.x = obj->margin_side;
    obj->base.y = _toast_hidden_y(obj); /* 初始停在屏外 */

    obj->text = NULL;
    obj->txt_ox = 0;
    obj->txt_oy = 0;
    obj->prefix_w = 0;
    obj->fit_mode = _TOAST_FIT_CENTER;
    obj->bg_color = RGB888TODEV(46, 52, 64);
    obj->text_color = RGB888TODEV(236, 241, 248);
    obj->opacity = 0U;

    obj->state = WE_TOAST_ST_HIDDEN;
    obj->duration_ms = WE_TOAST_DEF_DURATION;
    obj->phase_acc = 0U;
    obj->from_y = obj->base.y;
    obj->from_opa = 0U;

    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    /* 初始隐藏且停在屏外：不需要标脏 */
}

/**
 * @brief 弹出提示：滑入 → 停留 duration_ms → 滑出自动消失。
 * @param obj 控件对象指针。
 * @param text UTF-8 提示文本（调用方持有，需在显示期间保持有效）。
 * @param duration_ms 停留时长（毫秒），0 使用 WE_TOAST_DEF_DURATION。
 * @return 无。
 */
void we_toast_show(we_toast_obj_t *obj, const char *text, uint16_t duration_ms)
{
    if (obj == NULL || obj->base.lcd == NULL || text == NULL)
        return;

    we_obj_attach_to_top(obj->base.lcd, (we_obj_t *)obj); /* 顶层：保证置顶且不打乱普通层 Z 序 */

    obj->text = text;
    obj->duration_ms = (duration_ms == 0U) ? WE_TOAST_DEF_DURATION : duration_ms;
    _toast_layout(obj);

    /* 从当前位置/透明度重入：隐藏态即屏外/全透，动画中重复 show 不跳变 */
    obj->from_y = obj->base.y;
    obj->from_opa = obj->opacity;
    obj->state = WE_TOAST_ST_ENTER;
    obj->phase_acc = 0U;

    we_anim_start(obj->base.lcd, &obj->anim, _toast_anim_step_cb, obj);
    we_obj_invalidate((we_obj_t *)obj); /* 可见中换文本/换色立即重绘（屏外则被裁掉） */
}

/**
 * @brief 设置面板底色与文字色；两者均未变时直接返回。
 * @param obj 控件对象指针。
 * @param bg 面板底色。
 * @param text_color 文字颜色。
 * @return 无。
 */
void we_toast_set_colors(we_toast_obj_t *obj, colour_t bg, colour_t text_color)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (we_colour_eq(obj->bg_color, bg) && we_colour_eq(obj->text_color, text_color))
        return;

    obj->bg_color = bg;
    obj->text_color = text_color;
    if (obj->state != WE_TOAST_ST_HIDDEN && obj->opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置字体资源，并按新字体行高重算横幅高度。
 * @param obj 控件对象指针。
 * @param font 字体资源指针（NULL 或与当前相同直接返回）。
 * @return 无。
 */
void we_toast_set_font(we_toast_obj_t *obj, const unsigned char *font)
{
    uint16_t line_h;

    if (obj == NULL || obj->base.lcd == NULL || font == NULL || obj->font == font)
        return;

    if (obj->state != WE_TOAST_ST_HIDDEN)
        we_obj_invalidate((we_obj_t *)obj); /* 旧几何（旧高度）先标脏 */

    obj->font = font;
    line_h = we_font_get_line_height(font);
    if (line_h == 0U)
        line_h = 16U; /* 字库异常兜底 */
    obj->base.h = (int16_t)(line_h + 2 * WE_TOAST_PAD_Y);
    _toast_layout(obj);

    if (obj->state == WE_TOAST_ST_HIDDEN)
        obj->base.y = _toast_hidden_y(obj); /* 屏外停放位随高度重算，零标脏 */
    else
        we_obj_invalidate((we_obj_t *)obj); /* 新几何立即以新字体重绘 */
}

/**
 * @brief 设置停靠位与左右边距，并重算横幅宽度/位置。
 * @param obj 控件对象指针。
 * @param top 停靠 Y（滑入到位后的横幅顶边）。
 * @param side 左右边距（像素），宽度 = 屏宽 - 2*side。
 * @return 无。
 */
void we_toast_set_margin(we_toast_obj_t *obj, int16_t top, int16_t side)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (obj->margin_top == top && obj->margin_side == side)
        return;

    if (obj->state != WE_TOAST_ST_HIDDEN)
        we_obj_invalidate((we_obj_t *)obj); /* 旧几何先标脏 */

    obj->margin_top = top;
    obj->margin_side = side;
    obj->base.x = side;
    obj->base.w = (int16_t)((int16_t)obj->base.lcd->width - 2 * side);
    _toast_layout(obj);

    if (obj->state == WE_TOAST_ST_HIDDEN)
        return; /* 隐藏态：几何已就位（高度未变，屏外停放位不动），下次 show 生效 */

    we_obj_invalidate((we_obj_t *)obj); /* 新几何 */

    /* 滑入/停留中：复用重入机制从当前位置平滑滑到新停靠位
     * （停留计时随之重置——参数化调整属低频操作，可接受）；
     * 滑出中不干预，让其向屏外自然退场。 */
    if (obj->state == WE_TOAST_ST_ENTER || obj->state == WE_TOAST_ST_STAY)
    {
        obj->from_y = obj->base.y;
        obj->from_opa = obj->opacity;
        obj->state = WE_TOAST_ST_ENTER;
        obj->phase_acc = 0U;
        we_anim_start(obj->base.lcd, &obj->anim, _toast_anim_step_cb, obj);
    }
}

/**
 * @brief 删除控件（动画节点由内核随对象一并回收）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_toast_obj_delete(we_toast_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}
