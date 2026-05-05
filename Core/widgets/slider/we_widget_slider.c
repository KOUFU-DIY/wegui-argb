#include "we_widget_slider.h"
#include "we_render.h"

/**
 * @brief 控件绘制回调，向当前 PFB 输出滑条内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _slider_draw_cb(void *ptr);

/**
 * @brief 控件事件回调，处理按压、拖动和点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 表示事件已消费，0 表示无效。
 */
static uint8_t _slider_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);

static const we_class_t _slider_class = {
    .draw_cb = _slider_draw_cb,
    .event_cb = _slider_event_cb
};

static const colour_t _slider_black = RGB888_CONST(0, 0, 0);

/**
 * @brief 将输入值限制到滑条当前 min/max 范围内。
 * @param obj 滑条对象指针。
 * @param value 输入值。
 * @return 限制后的值。
 */
static uint8_t _slider_clamp_value(const we_slider_obj_t *obj, uint8_t value)
{
    if (obj == NULL)
        return value;
    if (value < obj->min_value)
        return obj->min_value;
    if (value > obj->max_value)
        return obj->max_value;
    return value;
}

/**
 * @brief 获取滑条当前数值跨度。
 * @param obj 滑条对象指针。
 * @return max_value - min_value；范围无效时返回 0。
 */
static uint8_t _slider_value_span(const we_slider_obj_t *obj)
{
    if (obj == NULL || obj->max_value <= obj->min_value)
        return 0U;
    return (uint8_t)(obj->max_value - obj->min_value);
}

/**
 * @brief 计算实际绘制用轨道厚度。
 * @param obj 滑条对象指针。
 * @return 轨道厚度（像素）。
 */
static uint8_t _slider_track_thickness(const we_slider_obj_t *obj)
{
    uint16_t thickness;
    uint16_t max_size;

    if (obj == NULL)
        return WE_SLIDER_TRACK_THICKNESS;

    thickness = obj->track_thickness ? obj->track_thickness : (uint16_t)WE_SLIDER_TRACK_THICKNESS;
    if (thickness < 2U)
        thickness = 2U;

    max_size = (obj->base.h > 0) ? (uint16_t)obj->base.h : 0U;
    if (max_size > 0U && thickness > max_size)
        thickness = max_size;
    max_size = (obj->base.w > 0) ? (uint16_t)obj->base.w : 0U;
    if (max_size > 0U && thickness > max_size)
        thickness = max_size;

    if (thickness > 255U)
        thickness = 255U;
    return (uint8_t)thickness;
}

/**
 * @brief 计算实际绘制用滑块尺寸。
 * @param obj 滑条对象指针。
 * @return 滑块尺寸（像素）。
 */
static uint8_t _slider_thumb_size(const we_slider_obj_t *obj)
{
    uint16_t size;
    uint16_t max_size;

    if (obj == NULL)
        return WE_SLIDER_THUMB_SIZE;

    size = obj->thumb_size ? obj->thumb_size : (uint16_t)WE_SLIDER_THUMB_SIZE;
#if (WE_SLIDER_ENABLE_VERTICAL == 1)
    max_size = (obj->orient == WE_SLIDER_ORIENT_VER) ? (uint16_t)obj->base.w : (uint16_t)obj->base.h;
#else
    max_size = (uint16_t)obj->base.h;
#endif
    if (max_size == 0U)
        return (uint8_t)size;
    if (size > max_size)
        size = max_size;
    if (size < 4U)
        size = 4U;
    if (size > 255U)
        size = 255U;
    return (uint8_t)size;
}

/**
 * @brief 计算滑块中心可移动的横向距离。
 * @param obj 滑条对象指针。
 * @param thumb_size 滑块尺寸（像素）。
 * @return 可移动距离（像素）。
 */
static uint16_t _slider_travel_len(const we_slider_obj_t *obj, uint8_t thumb_size)
{
    int16_t len;

    if (obj == NULL)
        return 0U;

#if (WE_SLIDER_ENABLE_VERTICAL == 1)
    if (obj->orient == WE_SLIDER_ORIENT_VER)
        len = (int16_t)obj->base.h - (int16_t)thumb_size;
    else
#endif
        len = (int16_t)obj->base.w - (int16_t)thumb_size;

    return (len > 0) ? (uint16_t)len : 0U;
}

/**
 * @brief 将指定数值映射为横向轨道填充长度。
 * @param obj 滑条对象指针。
 * @param value 输入值。
 * @param thumb_size 滑块尺寸（像素）。
 * @return 填充长度（像素）。
 */
static uint16_t _slider_fill_len_for_value(const we_slider_obj_t *obj, uint8_t value, uint8_t thumb_size)
{
    uint16_t travel_len;
    uint8_t span;

    travel_len = _slider_travel_len(obj, thumb_size);
    span = _slider_value_span(obj);
    if (travel_len == 0U || span == 0U)
        return 0U;

    value = _slider_clamp_value(obj, value);
    return (uint16_t)(((uint32_t)(value - obj->min_value) * (uint32_t)travel_len + (span / 2U)) / span);
}

/**
 * @brief 将当前值映射为横向轨道填充长度。
 * @param obj 滑条对象指针。
 * @param thumb_size 滑块尺寸（像素）。
 * @return 填充长度（像素）。
 */
static uint16_t _slider_fill_len(const we_slider_obj_t *obj, uint8_t thumb_size)
{
    if (obj == NULL)
        return 0U;
    return _slider_fill_len_for_value(obj, obj->value, thumb_size);
}

/**
 * @brief 标脏整个滑条控件区域。
 * @param obj 滑条对象指针。
 * @return 无。
 */
static void _slider_invalidate_full(we_slider_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 将指定绝对矩形裁剪到控件内部后标脏。
 * @param obj 滑条对象指针。
 * @param x0 脏区左上角 X 坐标。
 * @param y0 脏区左上角 Y 坐标。
 * @param w 脏区宽度。
 * @param h 脏区高度。
 * @return 无。
 */
static void _slider_invalidate_rect(we_slider_obj_t *obj, int16_t x0, int16_t y0, int16_t w, int16_t h)
{
    if (obj == NULL || w <= 0 || h <= 0)
        return;

    if (x0 < obj->base.x)
    {
        w = (int16_t)(w - (obj->base.x - x0));
        x0 = obj->base.x;
    }
    if (y0 < obj->base.y)
    {
        h = (int16_t)(h - (obj->base.y - y0));
        y0 = obj->base.y;
    }
    if (x0 + w > obj->base.x + obj->base.w)
        w = (int16_t)(obj->base.x + obj->base.w - x0);
    if (y0 + h > obj->base.y + obj->base.h)
        h = (int16_t)(obj->base.y + obj->base.h - y0);

    if (w > 0 && h > 0)
        we_obj_invalidate_area((we_obj_t *)obj, x0, y0, w, h);
}

/**
 * @brief 按指定填充长度标脏对应滑块外接区域。
 * @param obj 滑条对象指针。
 * @param fill_len 填充长度（像素）。
 * @return 无。
 */
static void _slider_invalidate_thumb_at_fill(we_slider_obj_t *obj, uint16_t fill_len)
{
    uint8_t thumb_size;
    int16_t half_thumb;
    int16_t center_x;
    int16_t center_y;
    int16_t x0;
    int16_t y0;

    if (obj == NULL)
        return;

    thumb_size = _slider_thumb_size(obj);
    half_thumb = (int16_t)(thumb_size / 2U);

#if (WE_SLIDER_ENABLE_VERTICAL == 1)
    if (obj->orient == WE_SLIDER_ORIENT_VER)
    {
        center_x = (int16_t)(obj->base.x + obj->base.w / 2);
        center_y = (int16_t)(obj->base.y + obj->base.h - half_thumb - (int16_t)fill_len);
    }
    else
#endif
    {
        center_x = (int16_t)(obj->base.x + half_thumb + (int16_t)fill_len);
        center_y = (int16_t)(obj->base.y + obj->base.h / 2);
    }

    x0 = (int16_t)(center_x - half_thumb);
    y0 = (int16_t)(center_y - half_thumb);

    _slider_invalidate_rect(obj, x0, y0, (int16_t)thumb_size, (int16_t)thumb_size);
}

/**
 * @brief 标脏当前滑块外接区域。
 * @param obj 滑条对象指针。
 * @return 无。
 */
static void _slider_invalidate_thumb(we_slider_obj_t *obj)
{
    uint8_t thumb_size;

    if (obj == NULL)
        return;

    thumb_size = _slider_thumb_size(obj);
    _slider_invalidate_thumb_at_fill(obj, _slider_fill_len(obj, thumb_size));
}

/**
 * @brief 数值变化时细分标脏旧滑块、新滑块和中间轨道区域。
 * @param obj 滑条对象指针。
 * @param old_value 变化前的数值。
 * @param new_value 变化后的数值。
 * @return 无。
 */
static void _slider_invalidate_value_change(we_slider_obj_t *obj, uint8_t old_value, uint8_t new_value)
{
    uint8_t thumb_size;
    uint16_t old_fill;
    uint16_t new_fill;
    uint16_t travel_len;
    int16_t half_thumb;
    int16_t rail_x;
    int16_t rail_y;
    int16_t track_pos0;
    int16_t track_span;
    uint8_t track_thickness;

    if (obj == NULL || old_value == new_value)
        return;

    thumb_size = _slider_thumb_size(obj);
    half_thumb = (int16_t)(thumb_size / 2U);
    track_thickness = _slider_track_thickness(obj);
    old_fill = _slider_fill_len_for_value(obj, old_value, thumb_size);
    new_fill = _slider_fill_len_for_value(obj, new_value, thumb_size);
    travel_len = _slider_travel_len(obj, thumb_size);

    _slider_invalidate_thumb_at_fill(obj, old_fill);
    _slider_invalidate_thumb_at_fill(obj, new_fill);

#if (WE_SLIDER_ENABLE_VERTICAL == 1)
    if (obj->orient == WE_SLIDER_ORIENT_VER)
    {
        uint16_t min_fill = (old_fill < new_fill) ? old_fill : new_fill;
        uint16_t max_fill = (old_fill > new_fill) ? old_fill : new_fill;

        rail_x = obj->base.x + (int16_t)((obj->base.w - (int16_t)track_thickness) / 2);
        rail_y = (int16_t)(obj->base.y + half_thumb);
        track_pos0 = (int16_t)(rail_y + (int16_t)(travel_len - max_fill));
        track_span = (int16_t)(max_fill - min_fill);
        _slider_invalidate_rect(obj, rail_x, track_pos0, (int16_t)track_thickness, track_span);
    }
    else
#endif
    {
        rail_x = (int16_t)(obj->base.x + half_thumb);
        rail_y = obj->base.y + (int16_t)((obj->base.h - (int16_t)track_thickness) / 2);
        track_pos0 = (int16_t)(rail_x + (int16_t)((old_fill < new_fill) ? old_fill : new_fill));
        track_span = (int16_t)(((old_fill > new_fill) ? old_fill : new_fill) - ((old_fill < new_fill) ? old_fill : new_fill));
        _slider_invalidate_rect(obj, track_pos0, rail_y, track_span, (int16_t)track_thickness);
    }
}

/**
 * @brief 将输入坐标转换为滑条数值。
 * @param obj 滑条对象指针。
 * @param x 输入点 X 坐标。
 * @param y 输入点 Y 坐标（当前横向实现未使用）。
 * @return 映射后的滑条数值。
 */
static uint8_t _slider_value_from_point(const we_slider_obj_t *obj, int16_t x, int16_t y)
{
    uint8_t thumb_size;
    uint16_t travel_len;
    uint8_t span;
    int16_t offset;
    uint16_t value;

    if (obj == NULL)
        return 0U;

    thumb_size = _slider_thumb_size(obj);
    travel_len = _slider_travel_len(obj, thumb_size);
    span = _slider_value_span(obj);
    if (travel_len == 0U || span == 0U)
        return obj->value;

#if (WE_SLIDER_ENABLE_VERTICAL == 1)
    if (obj->orient == WE_SLIDER_ORIENT_VER)
    {
        offset = (int16_t)(obj->base.y + obj->base.h - (int16_t)(thumb_size / 2U) - y);
    }
    else
#endif
    {
        offset = (int16_t)(x - obj->base.x - (int16_t)(thumb_size / 2U));
    }

    if (offset < 0)
        offset = 0;
    if (offset > (int16_t)travel_len)
        offset = (int16_t)travel_len;

    value = (uint16_t)obj->min_value + (uint16_t)(((uint32_t)offset * span + (travel_len / 2U)) / travel_len);
    return _slider_clamp_value(obj, (uint8_t)value);
}

/**
 * @brief 绘制滑条轨道或填充段。
 * @param obj 滑条对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 绘制区域左上角 X 坐标。
 * @param y 绘制区域左上角 Y 坐标。
 * @param w 绘制区域宽度。
 * @param h 绘制区域高度。
 * @param color 绘制颜色。
 * @return 无。
 */
static void _slider_draw_track(we_slider_obj_t *obj, we_lcd_t *lcd,
                               int16_t x, int16_t y, uint16_t w, uint16_t h,
                               colour_t color)
{
    if (obj == NULL || lcd == NULL || w == 0U || h == 0U)
        return;
    we_draw_round_rect_analytic_fill(lcd, x, y, w, h, (uint16_t)(h / 2U), color, obj->opacity);
}

/**
 * @brief 绘制滑块圆头。
 * @param obj 滑条对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 滑块外接框左上角 X 坐标。
 * @param y 滑块外接框左上角 Y 坐标。
 * @param size 滑块尺寸。
 * @param color 滑块颜色。
 * @return 无。
 */
static void _slider_draw_thumb(we_slider_obj_t *obj, we_lcd_t *lcd,
                               int16_t x, int16_t y, uint16_t size,
                               colour_t color)
{
    if (obj == NULL || lcd == NULL || size == 0U)
        return;
    we_draw_round_rect_analytic_fill(lcd, x, y, size, size, (uint16_t)(size / 2U), color, obj->opacity);
}

/**
 * @brief 控件绘制回调，绘制轨道、填充段和滑块圆头。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _slider_draw_cb(void *ptr)
{
    we_slider_obj_t *obj = (we_slider_obj_t *)ptr;
    we_lcd_t *lcd;
    uint8_t thumb_size;
    uint8_t track_thickness;
    uint16_t travel_len;
    uint16_t fill_len;
    int16_t rail_x;
    int16_t rail_y;
    int16_t thumb_x;
    int16_t thumb_y;
    int16_t thumb_cx;
    int16_t thumb_cy;
    colour_t thumb_color;

    if (obj == NULL || obj->opacity == 0U)
        return;

    lcd = obj->base.lcd;
    if (lcd == NULL)
        return;

    thumb_size = _slider_thumb_size(obj);
    track_thickness = _slider_track_thickness(obj);
    travel_len = _slider_travel_len(obj, thumb_size);
    fill_len = _slider_fill_len(obj, thumb_size);

#if (WE_SLIDER_ENABLE_VERTICAL == 1)
    if (obj->orient == WE_SLIDER_ORIENT_VER)
    {
        rail_x = obj->base.x + (int16_t)((obj->base.w - (int16_t)track_thickness) / 2);
        rail_y = (int16_t)(obj->base.y + thumb_size / 2U);
        thumb_cx = (int16_t)(obj->base.x + obj->base.w / 2);
        thumb_cy = (int16_t)(obj->base.y + obj->base.h - (int16_t)(thumb_size / 2U) - (int16_t)fill_len);

        _slider_draw_track(obj, lcd, rail_x, rail_y, track_thickness, travel_len, obj->track_color);
        _slider_draw_track(obj, lcd,
                           rail_x,
                           (int16_t)(rail_y + (int16_t)(travel_len - fill_len)),
                           track_thickness,
                           fill_len,
                           obj->fill_color);
    }
    else
#endif
    {
        rail_x = obj->base.x + (int16_t)(thumb_size / 2U);
        rail_y = obj->base.y + (int16_t)((obj->base.h - (int16_t)track_thickness) / 2);
        thumb_cx = (int16_t)(rail_x + (int16_t)fill_len);
        thumb_cy = (int16_t)(obj->base.y + obj->base.h / 2);

        _slider_draw_track(obj, lcd, rail_x, rail_y, travel_len, track_thickness, obj->track_color);
        _slider_draw_track(obj, lcd, rail_x, rail_y, fill_len, track_thickness, obj->fill_color);
    }

    thumb_x = (int16_t)(thumb_cx - (int16_t)(thumb_size / 2U));
    thumb_y = (int16_t)(thumb_cy - (int16_t)(thumb_size / 2U));

    if (obj->pressed)
        thumb_color = we_colour_blend(_slider_black, obj->thumb_color, 40U);
    else
        thumb_color = obj->thumb_color;

    _slider_draw_thumb(obj, lcd, thumb_x, thumb_y, thumb_size, thumb_color);
}

/**
 * @brief 控件事件回调，处理按下、拖动、释放和点击。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 表示事件已消费，0 表示无效。
 */
static uint8_t _slider_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_slider_obj_t *obj = (we_slider_obj_t *)ptr;
    uint8_t old_value;
    uint8_t new_value;

    if (obj == NULL)
        return 0U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        obj->pressed = 1U;
        if (data != NULL)
        {
            old_value = obj->value;
            new_value = _slider_value_from_point(obj, data->x, data->y);
            if (new_value != old_value)
            {
                obj->value = new_value;
                _slider_invalidate_value_change(obj, old_value, new_value);
            }
            else
            {
                _slider_invalidate_thumb(obj);
            }
        }
        else
        {
            _slider_invalidate_thumb(obj);
        }
        break;
    case WE_EVENT_STAY:
        if (obj->pressed && data != NULL)
        {
            old_value = obj->value;
            new_value = _slider_value_from_point(obj, data->x, data->y);
            if (new_value != old_value)
            {
                obj->value = new_value;
                _slider_invalidate_value_change(obj, old_value, new_value);
            }
        }
        break;
    case WE_EVENT_RELEASED:
        obj->pressed = 0U;
        _slider_invalidate_thumb(obj);
        break;
    case WE_EVENT_CLICKED:
        if (data != NULL)
        {
            old_value = obj->value;
            new_value = _slider_value_from_point(obj, data->x, data->y);
            if (new_value != old_value)
            {
                obj->value = new_value;
                _slider_invalidate_value_change(obj, old_value, new_value);
            }
        }
        break;
    default:
        break;
    }

    return 1U;
}

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 滑条对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 控件左上角 X 坐标。
 * @param y 控件左上角 Y 坐标。
 * @param w 控件宽度（像素）。
 * @param h 控件高度（像素）。
 * @param orient 滑条方向；支持横向和竖向。
 * @param min_value 最小值。
 * @param max_value 最大值。
 * @param init_value 初始值。
 * @param track_color 背景轨道颜色。
 * @param fill_color 已选择区间颜色。
 * @param thumb_color 滑块颜色。
 * @param opacity 整体透明度（0~255）。
 * @return 无。
 */
void we_slider_obj_init(we_slider_obj_t *obj, we_lcd_t *lcd,
                        int16_t x, int16_t y, uint16_t w, uint16_t h,
                        we_slider_orient_t orient,
                        uint8_t min_value, uint8_t max_value, uint8_t init_value,
                        colour_t track_color, colour_t fill_color,
                        colour_t thumb_color, uint8_t opacity)
{
    if (obj == NULL || lcd == NULL || w == 0U || h == 0U)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = (int16_t)w;
    obj->base.h = (int16_t)h;
    obj->base.class_p = &_slider_class;
    obj->base.parent = NULL;
    obj->base.next = NULL;

    if (min_value > max_value)
    {
        uint8_t tmp = min_value;
        min_value = max_value;
        max_value = tmp;
    }

    obj->min_value = min_value;
    obj->max_value = max_value;
    obj->value = init_value;
    obj->opacity = opacity;
    obj->track_thickness = (uint8_t)WE_SLIDER_TRACK_THICKNESS;
    obj->thumb_size = (uint8_t)WE_SLIDER_THUMB_SIZE;
    obj->pressed = 0U;
#if (WE_SLIDER_ENABLE_VERTICAL == 1)
    obj->orient = orient;
#else
    (void)orient;
    obj->orient = WE_SLIDER_ORIENT_HOR;
#endif
    obj->track_color = track_color;
    obj->fill_color = fill_color;
    obj->thumb_color = thumb_color;

    obj->value = _slider_clamp_value(obj, obj->value);

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

    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除滑条控件对象并从对象链表移除。
 * @param obj 滑条对象指针。
 * @return 无。
 */
void we_slider_obj_delete(we_slider_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}

/**
 * @brief 设置滑条当前值并按需标脏。
 * @param obj 滑条对象指针。
 * @param value 新值。
 * @return 无。
 */
void we_slider_set_value(we_slider_obj_t *obj, uint8_t value)
{
    uint8_t old_value;

    if (obj == NULL)
        return;

    value = _slider_clamp_value(obj, value);
    if (obj->value == value)
        return;

    old_value = obj->value;
    obj->value = value;
    _slider_invalidate_value_change(obj, old_value, value);
}

/**
 * @brief 在当前值基础上增加指定增量。
 * @param obj 滑条对象指针。
 * @param delta 增量值。
 * @return 无。
 */
void we_slider_add_value(we_slider_obj_t *obj, uint8_t delta)
{
    uint16_t sum;

    if (obj == NULL)
        return;

    sum = (uint16_t)obj->value + delta;
    if (sum > obj->max_value)
        sum = obj->max_value;
    we_slider_set_value(obj, (uint8_t)sum);
}

/**
 * @brief 在当前值基础上减少指定增量。
 * @param obj 滑条对象指针。
 * @param delta 减量值。
 * @return 无。
 */
void we_slider_sub_value(we_slider_obj_t *obj, uint8_t delta)
{
    if (obj == NULL)
        return;

    if (delta >= (uint8_t)(obj->value - obj->min_value))
        we_slider_set_value(obj, obj->min_value);
    else
        we_slider_set_value(obj, (uint8_t)(obj->value - delta));
}

/**
 * @brief 获取滑条当前值。
 * @param obj 滑条对象指针。
 * @return 当前值；obj 为 NULL 时返回 0。
 */
uint8_t we_slider_get_value(const we_slider_obj_t *obj)
{
    return (obj == NULL) ? 0U : obj->value;
}

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 滑条对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_slider_set_opacity(we_slider_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    _slider_invalidate_full(obj);
}

/**
 * @brief 设置轨道、填充区和滑块颜色。
 * @param obj 滑条对象指针。
 * @param track_color 背景轨道颜色。
 * @param fill_color 已选择区间颜色。
 * @param thumb_color 滑块颜色。
 * @return 无。
 */
void we_slider_set_colors(we_slider_obj_t *obj, colour_t track_color,
                          colour_t fill_color, colour_t thumb_color)
{
    if (obj == NULL)
        return;
    obj->track_color = track_color;
    obj->fill_color = fill_color;
    obj->thumb_color = thumb_color;
    _slider_invalidate_full(obj);
}

/**
 * @brief 设置滑块尺寸并按需重绘。
 * @param obj 滑条对象指针。
 * @param thumb_size 滑块尺寸（像素）。
 * @return 无。
 */
void we_slider_set_thumb_size(we_slider_obj_t *obj, uint8_t thumb_size)
{
    if (obj == NULL || obj->thumb_size == thumb_size)
        return;
    obj->thumb_size = thumb_size;
    _slider_invalidate_full(obj);
}

/**
 * @brief 设置轨道厚度并按需重绘。
 * @param obj 滑条对象指针。
 * @param track_thickness 轨道厚度（像素）。
 * @return 无。
 */
void we_slider_set_track_thickness(we_slider_obj_t *obj, uint8_t track_thickness)
{
    if (obj == NULL || obj->track_thickness == track_thickness)
        return;
    obj->track_thickness = track_thickness;
    _slider_invalidate_full(obj);
}
