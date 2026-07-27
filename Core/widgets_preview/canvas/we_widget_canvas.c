/**
 * @file  we_widget_canvas.c
 * @brief 用户自绘壳控件（preview）：PFB 窗口收窄 + 用户回调转发
 *
 * draw_cb 按 group 的标准套路把 PFB 窗口收窄到自身矩形
 * （save/restore pfb_area / pfb_y_start / pfb_y_end / pfb_gram），
 * 并把控件 opacity 乘入 lcd->opa_scale 级联乘子，然后调用用户自绘回调；
 * 回调返回后恢复窗口。用户回调内所有原语的越界像素被窗口自动裁掉。
 */

#include "we_widget_canvas.h"

/* --------------------------------------------------------------------------
 * 内部回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：收窄 PFB 窗口 → 级联透明度 → 转发用户自绘回调 → 恢复。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _canvas_draw_cb(void *ptr)
{
    we_canvas_obj_t *obj = (we_canvas_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;

    if (obj->user_draw_cb == NULL || obj->opacity == 0U)
        return;

    {
        /* group 同款 PFB 窗口收窄：用户回调里的原语越界自动被裁掉 */
        we_area_t old_pfb_area = lcd->pfb_area;
        uint16_t old_y_start = lcd->pfb_y_start;
        uint16_t old_y_end = lcd->pfb_y_end;
        colour_t *old_gram = lcd->pfb_gram;
        uint8_t old_scale = lcd->opa_scale;

        int16_t new_x0 = WE_MAX(old_pfb_area.x0, obj->base.x);
        int16_t new_y0 = WE_MAX((int16_t)old_y_start, obj->base.y);
        int16_t new_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(obj->base.x + obj->base.w - 1));
        int16_t new_y1 = WE_MIN((int16_t)old_y_end, (int16_t)(obj->base.y + obj->base.h - 1));

        if (new_x0 <= new_x1 && new_y0 <= new_y1)
        {
            /* 控件整体透明度乘入级联乘子：用户回调内原语按各自 opacity 传参即可 */
            lcd->opa_scale = we_opa_apply(lcd, obj->opacity);

            lcd->pfb_area.x0 = (uint16_t)new_x0;
            lcd->pfb_area.x1 = (uint16_t)new_x1;
            lcd->pfb_y_start = (uint16_t)new_y0;
            lcd->pfb_y_end = (uint16_t)new_y1;
            lcd->pfb_gram = old_gram + (new_y0 - (int16_t)old_y_start) * lcd->pfb_width +
                            (new_x0 - (int16_t)old_pfb_area.x0);

            obj->user_draw_cb(lcd, obj, obj->user_data);
        }

        lcd->opa_scale = old_scale;
        lcd->pfb_area = old_pfb_area;
        lcd->pfb_y_start = old_y_start;
        lcd->pfb_y_end = old_y_end;
        lcd->pfb_gram = old_gram;
    }
}

/**
 * @brief 控件事件回调：有用户回调则转发，否则返回 0 穿透。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备数据指针。
 * @return 1 已消费，0 穿透给背后控件。
 */
static uint8_t _canvas_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_canvas_obj_t *obj = (we_canvas_obj_t *)ptr;

    if (obj == NULL)
        return 0U;
    if (obj->user_event_cb != NULL)
        return obj->user_event_cb(ptr, event, data);

    (void)event;
    (void)data;
    return 0U; /* 默认装饰性，不消费事件 */
}

static const we_class_t _canvas_class = {
    .draw_cb    = _canvas_draw_cb,
    .event_cb   = _canvas_event_cb,
    .set_pos_cb = NULL
};

/* --------------------------------------------------------------------------
 * 生命周期与 setter
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化自绘壳控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 自绘区左上角 X（屏幕绝对坐标）。
 * @param y 自绘区左上角 Y。
 * @param w 自绘区宽度（像素）。
 * @param h 自绘区高度（像素）。
 * @param user_draw_cb 用户自绘回调（可为 NULL）。
 * @param user_data 业务上下文指针（原样透传，可为 NULL）。
 * @return 无。
 */
void we_canvas_obj_init(we_canvas_obj_t *obj, we_lcd_t *lcd,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        we_canvas_draw_cb_t user_draw_cb, void *user_data)
{
    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_canvas_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    obj->user_draw_cb = user_draw_cb;
    obj->user_data = user_data;
    obj->user_event_cb = NULL;
    obj->opacity = 255U;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置用户事件回调（非 NULL 时接管全部输入事件，NULL 恢复穿透）。
 * @param obj 控件对象指针。
 * @param cb 用户事件回调。
 * @return 无。
 */
void we_canvas_set_event_cb(we_canvas_obj_t *obj, we_canvas_event_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->user_event_cb = cb;
}

/**
 * @brief 请求重绘（透传 we_obj_invalidate，整框标脏）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_canvas_invalidate(we_canvas_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置整体不透明度并按需重绘；值未变直接返回。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255；0 时跳过用户回调）。
 * @return 无。
 */
void we_canvas_set_opacity(we_canvas_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->base.lcd == NULL || obj->opacity == opacity)
        return;

    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除控件并从对象链表移除（无动画节点，直接转调 we_obj_delete）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_canvas_obj_delete(we_canvas_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}
