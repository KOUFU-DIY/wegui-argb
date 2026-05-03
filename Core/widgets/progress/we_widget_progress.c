#include "we_widget_progress.h"
#include "we_motion.h"
#include "we_render.h"
#include "widgets/btn/we_widget_btn.h"

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _progress_draw_cb(void *ptr);

/**
 * @brief 控件事件回调，处理按压/滑动/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 返回状态标志（1 有效，0 无效）。
 */
static uint8_t _progress_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);

/**
 * @brief 周期任务回调，按时间步长推进本控件动画。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param user_data 任务回调用户数据指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _progress_task_cb(we_lcd_t *lcd, void *user_data, uint16_t elapsed_ms);

static const we_class_t _progress_class = {
    .draw_cb = _progress_draw_cb,
    .event_cb = _progress_event_cb
};

/**
 * @brief 将 0~255 的进度值换算为当前控件宽度下的填充像素宽度。
 * @param obj 进度条对象指针。
 * @param value 进度值（0~255）。
 * @return 对应的前景填充宽度（像素）。
 */
static uint16_t _progress_calc_fill_w(const we_progress_obj_t *obj, uint8_t value)
{
    uint32_t scaled;

    if (obj == NULL || obj->base.w <= 0)
        return 0U;

    if (value == 0U)
        return 0U;
    if (value >= 255U)
        return (uint16_t)obj->base.w;

    scaled = (uint32_t)((uint32_t)value * (uint32_t)obj->base.w / 255U);
    if (scaled > (uint32_t)obj->base.w)
        scaled = (uint32_t)obj->base.w;
    return (uint16_t)scaled;
}

/**
 * @brief 计算脏区并发起局部重绘请求。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _progress_invalidate(we_progress_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 计算脏区并发起局部重绘请求。
 * @param obj 目标控件对象指针。
 * @param old_fill_w 变更前的填充宽度。
 * @param new_fill_w 新的 fill_w 值。
 * @return 无。
 */
static void _progress_invalidate_fill_change(we_progress_obj_t *obj, uint16_t old_fill_w, uint16_t new_fill_w)
{
    int16_t x0;
    int16_t w;

    if (obj == NULL)
        return;

    if (old_fill_w == new_fill_w)
        return;

    x0 = (int16_t)(obj->base.x + ((old_fill_w < new_fill_w) ? old_fill_w : new_fill_w));
    w  = (int16_t)(((old_fill_w > new_fill_w) ? old_fill_w : new_fill_w) - ((old_fill_w < new_fill_w) ? old_fill_w : new_fill_w));

    if (w <= 0)
        return;

    if (obj->radius > 0U)
    {
        int16_t expand = (int16_t)(obj->radius + 1U);
        x0 -= expand;
        w  = (int16_t)(w + expand * 2);
    }

    if (x0 < obj->base.x)
    {
        w = (int16_t)(w - (obj->base.x - x0));
        x0 = obj->base.x;
    }

    if (x0 + w > obj->base.x + obj->base.w)
    {
        w = (int16_t)(obj->base.x + obj->base.w - x0);
    }

    if (w > 0)
    {
        we_obj_invalidate_area((we_obj_t *)obj, x0, obj->base.y, w, obj->base.h);
    }
}

/**
 * @brief 在当前 PFB 裁剪区内执行局部绘制。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 目标区域宽度（像素）。
 * @param color 目标颜色值。
 * @return 无。
 */
static void _progress_draw_left_corner_fill(we_progress_obj_t *obj, we_lcd_t *lcd, uint16_t w, colour_t color)
{
    we_area_t saved_area;
    uint16_t r;

    if (obj == NULL || lcd == NULL || w == 0U)
        return;

    r = obj->radius;
    if (r == 0U)
    {
        we_fill_rect(lcd, obj->base.x, obj->base.y, w, (uint16_t)obj->base.h, color, obj->opacity);
        return;
    }

    if (r > (uint16_t)obj->base.h / 2U)
        r = (uint16_t)obj->base.h / 2U;

    saved_area = lcd->pfb_area;
    if ((uint16_t)(obj->base.x + w - 1U) < lcd->pfb_area.x1)
        lcd->pfb_area.x1 = (uint16_t)(obj->base.x + w - 1U);

    if (w > r)
    {
        we_fill_rect(lcd, obj->base.x + r, obj->base.y, (uint16_t)(w - r), (uint16_t)obj->base.h, color, obj->opacity);
    }

    if (obj->base.h > (int16_t)(r * 2U))
    {
        we_fill_rect(lcd, obj->base.x, obj->base.y + r, r, (uint16_t)(obj->base.h - r * 2U), color, obj->opacity);
    }
    we_draw_quarter_circle_analytic(lcd, obj->base.x, obj->base.y, r, color, obj->opacity, WE_MASK_QUADRANT_LT);
    we_draw_quarter_circle_analytic(lcd, obj->base.x, obj->base.y + (int16_t)obj->base.h - (int16_t)r, r,
                                    color, obj->opacity, WE_MASK_QUADRANT_LB);

    lcd->pfb_area = saved_area;
}

/**
 * @brief 在当前 PFB 裁剪区内执行局部绘制。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param color 目标颜色值。
 * @return 无。
 */
static void _progress_draw_track_bar(we_progress_obj_t *obj, we_lcd_t *lcd, colour_t color)
{
    uint16_t draw_r;

    if (obj == NULL || lcd == NULL)
        return;

    draw_r = obj->radius;
    if (draw_r > (uint16_t)obj->base.h / 2U)
        draw_r = (uint16_t)obj->base.h / 2U;

#if (WE_PROGRESS_USE_BTN_SKIN == 1)
    {
        we_btn_style_t style = {
            .bg_color = color,
            .border_color = color,
            .text_color = color,
            .border_w = 0U
        };
        we_btn_draw_skin(lcd, obj->base.x, obj->base.y, (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                         draw_r, &style, obj->opacity);
    }
#else
    we_draw_round_rect(lcd, obj->base.x, obj->base.y, (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                       draw_r, color, obj->opacity);
#endif
}

/**
 * @brief 在当前 PFB 裁剪区内执行局部绘制。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 目标区域宽度（像素）。
 * @param color 目标颜色值。
 * @return 无。
 */
static void _progress_draw_fill_bar(we_progress_obj_t *obj, we_lcd_t *lcd, uint16_t w, colour_t color)
{
    uint16_t r;

    if (w == 0U)
        return;

    r = obj->radius;
    if (r > (uint16_t)obj->base.h / 2U)
        r = (uint16_t)obj->base.h / 2U;

    /* 前景进度始终保留左侧圆角；
     * 当进度进入右端圆角区域后，直接对“完整圆角条”做右侧裁剪，
     * 这样顶部/底部会沿着右圆角自然收边，并且 255 时能完整贴满轨道。 */
    if (r > 0U && w > (uint16_t)(obj->base.w - r))
    {
        we_area_t saved_area = lcd->pfb_area;
        int16_t clip_x1 = (int16_t)(obj->base.x + w - 1U);

        if (clip_x1 < (int16_t)lcd->pfb_area.x0)
            return;
        if (clip_x1 < (int16_t)lcd->pfb_area.x1)
            lcd->pfb_area.x1 = (uint16_t)clip_x1;

        _progress_draw_track_bar(obj, lcd, color);
        lcd->pfb_area = saved_area;
        return;
    }

    _progress_draw_left_corner_fill(obj, lcd, w, color);
}

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _progress_draw_cb(void *ptr)
{
    we_progress_obj_t *obj = (we_progress_obj_t *)ptr;
    we_lcd_t *lcd;
    uint16_t fill_w;

    if (obj == NULL || obj->opacity == 0U)
        return;

    lcd = obj->base.lcd;
    if (lcd == NULL)
        return;

    _progress_draw_track_bar(obj, lcd, obj->track_color);

    fill_w = _progress_calc_fill_w(obj, obj->display_value);
    if (fill_w > 0U)
    {
        _progress_draw_fill_bar(obj, lcd, fill_w, obj->fill_color);
    }
}

/**
 * @brief 控件事件回调，处理按压/滑动/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 返回状态标志（1 有效，0 无效）。
 */
static uint8_t _progress_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    (void)ptr;
    (void)event;
    (void)data;
    return 0U;
}

/**
 * @brief 推进一次进度条动画状态。
 * @param obj 目标控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _progress_anim_step(we_progress_obj_t *obj, uint16_t elapsed_ms)
{
    uint16_t t;
    uint16_t eased;
    uint16_t old_fill_w;
    uint16_t new_fill_w;
    uint8_t next_value;

    if (obj == NULL)
        return;

    if (obj->animating)
    {
        old_fill_w = _progress_calc_fill_w(obj, obj->display_value);

        obj->anim_elapsed_ms += elapsed_ms;
        if (obj->anim_elapsed_ms >= obj->anim_duration_ms)
        {
            obj->anim_elapsed_ms = obj->anim_duration_ms;
            obj->display_value = obj->anim_to_value;
            obj->animating = 0U;
        }
        else
        {
            t = (uint16_t)(((uint32_t)obj->anim_elapsed_ms << 8) / obj->anim_duration_ms);
            eased = we_ease_out_cubic(t);
            next_value = (uint8_t)we_lerp(obj->anim_from_value, obj->anim_to_value, eased);

            if (next_value == obj->display_value && obj->display_value != obj->anim_to_value)
            {
                if (obj->anim_to_value > obj->display_value)
                    next_value = (uint8_t)(obj->display_value + 1U);
                else
                    next_value = (uint8_t)(obj->display_value - 1U);
            }
            obj->display_value = next_value;
        }

        new_fill_w = _progress_calc_fill_w(obj, obj->display_value);
        _progress_invalidate_fill_change(obj, old_fill_w, new_fill_w);
    }
}

/**
 * @brief 周期任务回调，按时间步长推进本控件动画。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param user_data 任务回调用户数据指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _progress_task_cb(we_lcd_t *lcd, void *user_data, uint16_t elapsed_ms)
{
    (void)lcd;
    _progress_anim_step((we_progress_obj_t *)user_data, elapsed_ms);
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

    obj->task_id = -1;
    obj->value = init_value;
    obj->display_value = init_value;
    obj->anim_from_value = init_value;
    obj->anim_to_value = init_value;
    obj->anim_elapsed_ms = 0U;
    obj->anim_duration_ms = WE_PROGRESS_ANIM_DURATION_MS;
    obj->animating = 0U;
    obj->track_color = track_color;
    obj->fill_color = fill_color;
    obj->opacity = opacity;
    obj->radius = WE_PROGRESS_RADIUS;

    if (lcd->obj_list_head == NULL)
    {
        lcd->obj_list_head = (we_obj_t *)obj;
    }
    else
    {
        we_obj_t *tail = lcd->obj_list_head;
        while (tail->next != NULL)
            tail = tail->next;
        tail->next = (we_obj_t *)obj;
    }

    obj->task_id = _we_gui_task_register_with_data(lcd, _progress_task_cb, obj);
}

/**
 * @brief 释放控件运行时状态并从任务系统注销。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_progress_obj_delete(we_progress_obj_t *obj)
{
    if (obj == NULL)
        return;
    if (obj->task_id >= 0 && obj->base.lcd != NULL)
    {
        _we_gui_task_unregister(obj->base.lcd, obj->task_id);
        obj->task_id = -1;
    }
    we_obj_delete((we_obj_t *)obj);
}

/**
 * @brief 更新属性并同步显示状态。
 * @param obj 目标控件对象指针。
 * @param value 输入采样值。
 * @return 无。
 */
void we_progress_set_value(we_progress_obj_t *obj, uint8_t value)
{
    uint16_t old_fill_w;
    uint16_t new_fill_w;
    uint8_t diff;

    if (obj == NULL)
        return;

    if (value == obj->value)
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
    }

    new_fill_w = _progress_calc_fill_w(obj, obj->display_value);
    _progress_invalidate_fill_change(obj, old_fill_w, new_fill_w);
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
 * @return 返回对应计算结果。
 */
uint8_t we_progress_get_value(const we_progress_obj_t *obj)
{
    return (obj == NULL) ? 0U : obj->value;
}

/**
 * @brief 读取当前显示进度值。
 * @param obj 目标控件对象指针。
 * @return 返回对应计算结果。
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
    _progress_invalidate(obj);
}

/**
 * @brief 更新属性并同步显示状态。
 * @param obj 目标控件对象指针。
 * @param radius 圆角半径（像素）。
 * @return 无。
 */
void we_progress_set_radius(we_progress_obj_t *obj, uint8_t radius)
{
    if (obj == NULL || obj->radius == radius)
        return;
    obj->radius = radius;
    _progress_invalidate(obj);
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
    obj->track_color = track_color;
    obj->fill_color = fill_color;
    _progress_invalidate(obj);
}
