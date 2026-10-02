#include "we_widget_progress.h"
#include "components/we_motion.h"
#include "wegui.h"

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _progress_draw_cb(void *ptr);

/**
 * @brief 中央动画引擎回调，按时间步长推进本控件动画。
 * @param owner 控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _progress_anim_step_cb(void *owner, uint16_t elapsed_ms);

/* 不处理输入：event_cb 为 NULL，命中测试跳过它，触摸落到背后的控件 */
static const we_class_t _progress_class = {
    .draw_cb = _progress_draw_cb,
    .event_cb = NULL,
    .set_pos_cb = NULL
};

/**
 * @brief 将 0~255 的进度值换算为当前控件宽度下的填充像素宽度。
 * @param obj 进度条对象指针。
 * @param value 进度值（0~255）。
 * @return 对应的前景填充宽度（像素）。
 */
static uint16_t _progress_calc_fill_w(const we_progress_obj_t *obj, uint8_t value)
{
    if (obj->base.w <= 0 || value == 0U)
        return 0U;
    if (value >= 255U)
        return (uint16_t)obj->base.w;
    return (uint16_t)((uint32_t)value * (uint32_t)obj->base.w / 255U);
}

/**
 * @brief 进度宽度变化时只标脏扫过的那段列带。
 * @param obj 目标控件对象指针。
 * @param old_fill_w 变更前的填充宽度。
 * @param new_fill_w 变更后的填充宽度。
 * @return 无。
 * @note 已填段是“整条圆角轨道裁到填充宽度”，同一列的像素只取决于它在不在
 *       填充宽度以内，所以左侧不变的部分两次绘制完全相同，只需标
 *       [min, max) 这段列带，不必按圆角半径向两侧扩张。
 */
static void _progress_invalidate_fill_change(we_progress_obj_t *obj, uint16_t old_fill_w, uint16_t new_fill_w)
{
    uint16_t min_fill_w;
    uint16_t max_fill_w;

    if (old_fill_w == new_fill_w)
        return;

    min_fill_w = (old_fill_w < new_fill_w) ? old_fill_w : new_fill_w;
    max_fill_w = (old_fill_w > new_fill_w) ? old_fill_w : new_fill_w;
    we_obj_invalidate_area((we_obj_t *)obj, (int16_t)(obj->base.x + (int16_t)min_fill_w), obj->base.y,
                           (int16_t)(max_fill_w - min_fill_w), obj->base.h);
}

/**
 * @brief 在当前窗口内绘制整条圆角轨道。
 * @param obj 目标控件对象指针。
 * @param color 目标颜色值。
 * @return 无。
 * @note 圆角半径超过宽高一半时由绘图原语自行钳制。
 */
static void _progress_draw_track_bar(we_progress_obj_t *obj, colour_t color)
{
    we_draw_round_rect_analytic_fill(obj->base.lcd, obj->base.x, obj->base.y,
                                     (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                     obj->radius, color, obj->opacity);
}

/**
 * @brief 绘制已填段：把窗口收窄到左侧 fill_w 列，再画一整条圆角轨道。
 * @param obj 目标控件对象指针。
 * @param fill_w 已填宽度（像素）。
 * @param color 填充颜色。
 * @return 无。
 * @note 左右两端沿用整条轨道的圆角，填充很短时左端圆角也不会被削平。
 */
static void _progress_draw_fill(we_progress_obj_t *obj, uint16_t fill_w, colour_t color)
{
    we_lcd_t *lcd = obj->base.lcd;
    we_pfb_clip_t sv;

    if (fill_w == 0U)
        return;
    we_pfb_clip_save(lcd, &sv);
    if (we_pfb_clip_to(lcd, &sv, obj->base.x, obj->base.y,
                       (int16_t)(obj->base.x + (int16_t)fill_w - 1),
                       (int16_t)(obj->base.y + obj->base.h - 1)))
    {
        _progress_draw_track_bar(obj, color);
        we_pfb_clip_restore(lcd, &sv);
    }
}

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _progress_draw_cb(void *ptr)
{
    we_progress_obj_t *obj = (we_progress_obj_t *)ptr;

    if (obj->opacity == 0U)
        return;

#if (WE_CFG_MONO_STYLE == 1)
    /* 单色样式：描边轨道 + 实心已填段 */
    {
        static const colour_t mono_fg = RGB888_CONST(255, 255, 255);
        static const colour_t mono_bg = RGB888_CONST(0, 0, 0);

        we_draw_round_rect_outline_fill(obj->base.lcd, obj->base.x, obj->base.y,
                                        (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                        obj->radius, mono_fg, mono_bg, obj->opacity);
        _progress_draw_fill(obj, _progress_calc_fill_w(obj, obj->display_value), mono_fg);
    }
#else
    _progress_draw_track_bar(obj, obj->track_color);
    _progress_draw_fill(obj, _progress_calc_fill_w(obj, obj->display_value), obj->fill_color);
#endif
}

/**
 * @brief 推进一次进度条动画状态。
 * @param obj 目标控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _progress_anim_step(we_progress_obj_t *obj, uint16_t elapsed_ms)
{
    uint16_t old_fill_w;
    uint32_t acc;

    if (!obj->animating)
    {
        /* 已无动画在途：摘链停表，空闲期零开销 */
        we_anim_stop(obj->base.lcd, &obj->anim);
        return;
    }

    old_fill_w = _progress_calc_fill_w(obj, obj->display_value);

    acc = (uint32_t)obj->anim_elapsed_ms + elapsed_ms;
    if (acc >= WE_PROGRESS_ANIM_DURATION_MS)
    {
        obj->anim_elapsed_ms = (uint16_t)WE_PROGRESS_ANIM_DURATION_MS;
        obj->display_value = obj->anim_to_value;
        obj->animating = 0U;
        we_anim_stop(obj->base.lcd, &obj->anim); /* 本步到位 */
    }
    else
    {
        uint16_t t = (uint16_t)((acc << 8) / WE_PROGRESS_ANIM_DURATION_MS);
        uint8_t next_value = (uint8_t)we_lerp(obj->anim_from_value, obj->anim_to_value, we_ease_out_cubic(t));

        obj->anim_elapsed_ms = (uint16_t)acc;
        /* 缓动开头/结尾一帧可能不前进：至少走 1 个值，保证动画不会停在中途 */
        if (next_value == obj->display_value && obj->display_value != obj->anim_to_value)
        {
            if (obj->anim_to_value > obj->display_value)
                next_value = (uint8_t)(obj->display_value + 1U);
            else
                next_value = (uint8_t)(obj->display_value - 1U);
        }
        obj->display_value = next_value;
    }

    _progress_invalidate_fill_change(obj, old_fill_w, _progress_calc_fill_w(obj, obj->display_value));
}

/**
 * @brief 中央动画引擎回调，按时间步长推进本控件动画。
 * @param owner 控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _progress_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    _progress_anim_step((we_progress_obj_t *)owner, elapsed_ms);
}

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param init_value 初始进度值（0~255）。
 * @param track_color 轨道背景颜色。
 * @param fill_color 前景填充颜色。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_progress_obj_init(we_progress_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, uint16_t w, uint16_t h,
                          uint8_t init_value,
                          colour_t track_color, colour_t fill_color, uint8_t opacity)
{
    if (obj == NULL || lcd == NULL || w == 0U || h == 0U)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = (int16_t)w;
    obj->base.h = (int16_t)h;
    obj->base.class_p = &_progress_class;
    obj->base.parent = NULL;
    obj->base.next = NULL;

    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;
    obj->value = init_value;
    obj->display_value = init_value;
    obj->anim_from_value = init_value;
    obj->anim_to_value = init_value;
    obj->anim_elapsed_ms = 0U;
    obj->animating = 0U;
    obj->track_color = track_color;
    obj->fill_color = fill_color;
    obj->opacity = opacity;
    obj->radius = WE_PROGRESS_RADIUS;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置目标进度值；与显示值差值>1 时启用缓动动画，<=1 时直接到位避免高频抖动。
 * @param obj 目标控件对象指针。
 * @param value 新进度值（0~255）。
 * @return 无。
 */
void we_progress_set_value(we_progress_obj_t *obj, uint8_t value)
{
    uint16_t old_fill_w;
    uint8_t diff;

    if (obj == NULL || value == obj->value)
        return;

    old_fill_w = _progress_calc_fill_w(obj, obj->display_value);

    obj->value = value;
    diff = (obj->display_value > value) ? (uint8_t)(obj->display_value - value) : (uint8_t)(value - obj->display_value);

    /* 小步进变化不走缓动，直接一步到位。
     * 否则当外部持续以 +1/-1 高频推值时，动画会反复重启，显示值会产生抖动。 */
    if (diff <= 1U)
    {
        obj->display_value = value;
        obj->anim_from_value = value;
        obj->anim_to_value = value;
        obj->anim_elapsed_ms = 0U;
        obj->animating = 0U;
    }
    else
    {
        obj->anim_from_value = obj->display_value;
        obj->anim_to_value = value;
        obj->anim_elapsed_ms = 0U;
        obj->animating = 1U;
        /* 挂入中央动画链表（不会失败） */
        we_anim_start(obj->base.lcd, &obj->anim, _progress_anim_step_cb, obj);
    }

    _progress_invalidate_fill_change(obj, old_fill_w, _progress_calc_fill_w(obj, obj->display_value));
}

/**
 * @brief 在当前进度值基础上增加指定增量。
 * @param obj 目标控件对象指针。
 * @param delta 进度增量值。
 * @return 无。
 */
void we_progress_add_value(we_progress_obj_t *obj, uint8_t delta)
{
    uint16_t sum;

    if (obj == NULL)
        return;

    sum = (uint16_t)obj->value + delta;
    if (sum > 255U)
        sum = 255U;
    we_progress_set_value(obj, (uint8_t)sum);
}

/**
 * @brief 在当前进度值基础上减少指定增量。
 * @param obj 目标控件对象指针。
 * @param delta 进度增量值。
 * @return 无。
 */
void we_progress_sub_value(we_progress_obj_t *obj, uint8_t delta)
{
    if (obj == NULL)
        return;

    if (delta >= obj->value)
        we_progress_set_value(obj, 0U);
    else
        we_progress_set_value(obj, (uint8_t)(obj->value - delta));
}

/**
 * @brief 读取目标进度值。
 * @param obj 目标控件对象指针。
 * @return 目标进度值（0~255）；obj 为 NULL 时返回 0。
 */
uint8_t we_progress_get_value(const we_progress_obj_t *obj)
{
    return (obj == NULL) ? 0U : obj->value;
}

/**
 * @brief 读取当前显示进度值。
 * @param obj 目标控件对象指针。
 * @return 当前显示进度值（动画中间值，0~255）；obj 为 NULL 时返回 0。
 */
uint8_t we_progress_get_display_value(const we_progress_obj_t *obj)
{
    return (obj == NULL) ? 0U : obj->display_value;
}

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_progress_set_opacity(we_progress_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置轨道圆角半径并按需重绘。
 * @param obj 目标控件对象指针。
 * @param radius 圆角半径（像素）。
 * @return 无。
 */
void we_progress_set_radius(we_progress_obj_t *obj, uint8_t radius)
{
    if (obj == NULL || obj->radius == radius)
        return;
    obj->radius = radius;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置绘制颜色并刷新显示。
 * @param obj 目标控件对象指针。
 * @param track_color 轨道背景颜色。
 * @param fill_color 前景填充颜色。
 * @return 无。
 */
void we_progress_set_colors(we_progress_obj_t *obj, colour_t track_color,
                            colour_t fill_color)
{
    if (obj == NULL)
        return;
    if (we_colour_eq(obj->track_color, track_color) && we_colour_eq(obj->fill_color, fill_color))
        return;
    obj->track_color = track_color;
    obj->fill_color = fill_color;
    we_obj_invalidate((we_obj_t *)obj);
}
