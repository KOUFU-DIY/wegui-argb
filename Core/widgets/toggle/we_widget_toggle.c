#include "we_widget_toggle.h"
#include "we_render.h"

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _toggle_draw_cb(void *ptr);

/**
 * @brief 控件事件回调，处理按压/滑动/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 返回状态标志（1 有效，0 无效）。
 */
static uint8_t _toggle_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);
#if WE_TOGGLE_USE_ANIM

/**
 * @brief 周期回调中按时间片推进动画状态。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param user_data 任务回调用户数据指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _toggle_anim_task(we_lcd_t *lcd, void *user_data, uint16_t elapsed_ms);
#endif

static const we_class_t _toggle_class = {
    .draw_cb  = _toggle_draw_cb,
    .event_cb = _toggle_event_cb
};

/* --------------------------------------------------------------------------
 * 颜色常量（编译期生成，存放 Flash）
 * -------------------------------------------------------------------------- */
static const colour_t _c_on    = RGB888_CONST(WE_TOGGLE_COLOR_ON_R,
                                               WE_TOGGLE_COLOR_ON_G,
                                               WE_TOGGLE_COLOR_ON_B);
static const colour_t _c_off   = RGB888_CONST(WE_TOGGLE_COLOR_OFF_R,
                                               WE_TOGGLE_COLOR_OFF_G,
                                               WE_TOGGLE_COLOR_OFF_B);
static const colour_t _c_thumb = RGB888_CONST(WE_TOGGLE_COLOR_THUMB_R,
                                               WE_TOGGLE_COLOR_THUMB_G,
                                               WE_TOGGLE_COLOR_THUMB_B);
static const colour_t _c_black = RGB888_CONST(0, 0, 0);

#if WE_TOGGLE_USE_ANIM

/**
 * @brief 周期回调中按时间片推进动画状态。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @return 无。
 */
static int8_t _toggle_find_anim_task_id(we_lcd_t *lcd)
{
    uint8_t i;

    if (lcd == NULL)
        return -1;

    for (i = 0; i < WE_CFG_GUI_TASK_MAX_NUM; i++)
    {
        if (lcd->task_list[i].cb == NULL)
            continue;
        if (lcd->task_list[i].cb == _toggle_anim_task)
            return (int8_t)i;
    }

    return -1;
}

/**
 * @brief 内部辅助：toggle_anim_advance_obj。
 * @param obj 目标控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _toggle_anim_advance_obj(we_toggle_obj_t *obj, uint16_t elapsed_ms)
{
    uint8_t target;
    uint16_t acc_ms;
    uint16_t steps;

    if (obj == NULL || elapsed_ms == 0U)
        return;

    target = obj->checked ? (uint8_t)WE_TOGGLE_ANIM_STEPS : 0U;
    if (obj->anim_step == target)
    {
        obj->anim_acc_ms = 0U;
        return;
    }

    acc_ms = (uint16_t)(obj->anim_acc_ms + elapsed_ms);
    steps = (uint16_t)(acc_ms / (uint16_t)WE_TOGGLE_ANIM_STEP_MS);
    obj->anim_acc_ms = (uint16_t)(acc_ms % (uint16_t)WE_TOGGLE_ANIM_STEP_MS);

    if (steps == 0U)
        return;

    if (obj->anim_step < target)
    {
        uint16_t next = (uint16_t)obj->anim_step + steps;
        obj->anim_step = (next >= target) ? target : (uint8_t)next;
    }
    else
    {
        obj->anim_step = (steps >= obj->anim_step - target) ? target
                                                             : (uint8_t)(obj->anim_step - steps);
    }

    if (obj->anim_step == target)
        obj->anim_acc_ms = 0U;

    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 周期回调中按时间片推进动画状态。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param user_data 任务回调用户数据指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _toggle_anim_task(we_lcd_t *lcd, void *user_data, uint16_t elapsed_ms)
{
    we_obj_t *node;
    uint8_t has_toggle = 0U;

    (void)user_data;

    if (lcd == NULL || elapsed_ms == 0U)
        return;

    for (node = lcd->obj_list_head; node != NULL; node = node->next)
    {
        if (node->class_p != &_toggle_class)
            continue;

        has_toggle = 1U;
        _toggle_anim_advance_obj((we_toggle_obj_t *)node, elapsed_ms);
    }

    if (has_toggle == 0U)
    {
        int8_t task_id = _toggle_find_anim_task_id(lcd);
        if (task_id >= 0)
            _we_gui_task_unregister(lcd, task_id);
    }
}

/**
 * @brief 周期回调中按时间片推进动画状态。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @return 无。
 */
static int8_t _toggle_ensure_anim_task(we_lcd_t *lcd)
{
    int8_t task_id;

    if (lcd == NULL)
        return -1;

    task_id = _toggle_find_anim_task_id(lcd);
    if (task_id >= 0)
        return task_id;

    return _we_gui_task_register_with_data(lcd, _toggle_anim_task, NULL);
}
#else

/**
 * @brief 周期回调中按时间片推进动画状态。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @return 无。
 */
static int8_t _toggle_ensure_anim_task(we_lcd_t *lcd)
{
    (void)lcd;
    return -1;
}
#endif

/* --------------------------------------------------------------------------
 * 绘图回调
 *
 * 布局示意（h=24, w=44, pad=2）：
 *
 *   OFF：  ┌──────────────────────────┐
 *          │ ●                        │
 *          └──────────────────────────┘
 *
 *   ON：   ┌──────────────────────────┐
 *          │                        ● │
 *          └──────────────────────────┘
 *
 * 轨道为全胶囊形（radius = h/2），滑块为圆形（radius = thumb_d/2）。
 * 动画推进由 GUI 内部共享 task 统一驱动，
 * draw_cb 仅根据当前 anim_step 绘制，不修改任何状态、不标脏。
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _toggle_draw_cb(void *ptr)
{
    we_toggle_obj_t *obj = (we_toggle_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    int16_t   x   = obj->base.x;
    int16_t   y   = obj->base.y;
    int16_t   w   = obj->base.w;
    int16_t   h   = obj->base.h;
    int16_t   track_y;
    int16_t   track_h;

    if (obj->opacity == 0 || w <= 0 || h <= 0)
        return;

    /* ---- 1. 计算动画进度 ------------------------------------------ */
#if WE_TOGGLE_USE_ANIM
    uint8_t step        = obj->anim_step;
    /* blend_alpha: 0=完全 OFF 色，255=完全 ON 色 */
    uint8_t blend_alpha = (uint8_t)((uint32_t)step * 255U / (uint32_t)WE_TOGGLE_ANIM_STEPS);
#else
    uint8_t blend_alpha = obj->checked ? 255U : 0U;
#endif

    /* ---- 2. 计算轨道颜色 ------------------------------------------ */
colour_t track_color = we_colour_blend(_c_on, _c_off, blend_alpha);
    if (obj->pressed)
    {
        /* 按下时向黑色混合，产生轻微暗化的按压反馈 */
        track_color = we_colour_blend(_c_black, track_color,
                                      (uint8_t)WE_TOGGLE_PRESS_DARKEN);
    }

    track_y = y;
    track_h = h;
    if (h > 6)
    {
        track_y = (int16_t)(y + 1);
        track_h = (int16_t)(h - 2);
    }

    /* ---- 3. 计算滑块位置 ------------------------------------------ */
    int16_t pad     = (int16_t)WE_TOGGLE_THUMB_PAD;
    int16_t thumb_d = h - 2 * pad;
    int16_t thumb_cy;
    if (thumb_d < 2) thumb_d = 2;
    if ((thumb_d & 1) == 0 && thumb_d > 3)
        thumb_d -= 1; /* 奇数直径优先，圆心落在整数像素上更利落 */

    /* 滑块中心直接对齐轨道左右端圆心，避免与内缩后的 track 几何脱节 */
    int16_t thumb_cx_off = (int16_t)(x + track_h / 2);
    int16_t thumb_cx_on  = (int16_t)(x + w - track_h / 2);
    int16_t thumb_cx;

#if WE_TOGGLE_USE_ANIM
    thumb_cx = (int16_t)((int32_t)thumb_cx_off
                       + ((int32_t)(thumb_cx_on - thumb_cx_off) * step
                          + (int32_t)WE_TOGGLE_ANIM_STEPS / 2)
                       / (int32_t)WE_TOGGLE_ANIM_STEPS);
#else
    thumb_cx = obj->checked ? thumb_cx_on : thumb_cx_off;
#endif
    thumb_cy = (int16_t)(track_y + track_h / 2);

    /* ---- 4. 绘制轨道（radius = h/2 的圆角矩形，几何上等价于胶囊） -- */
    we_draw_round_rect_analytic_fill(lcd, x, track_y,
                                     (uint16_t)w, (uint16_t)track_h,
                                     (uint16_t)(track_h / 2),
                                     track_color, obj->opacity);

    /* ---- 5. 绘制滑块（w == h 且 radius = w/2 时退化为圆） ----------- */
    {
        int16_t tr = (int16_t)(thumb_d / 2);
        we_draw_round_rect_analytic_fill(lcd,
                                         (int16_t)(thumb_cx - tr),
                                         (int16_t)(thumb_cy - tr),
                                         (uint16_t)(tr * 2),
                                         (uint16_t)(tr * 2),
                                         (uint16_t)tr,
                                         _c_thumb, obj->opacity);
    }

    /* draw_cb 仅负责按当前 anim_step 绘制，不推进状态、不标脏。
     * 动画推进由 GUI 内部共享 task 完成，
     * 因此本帧脏矩形会在 flush 之前登记，不会被同帧 clear 掉。 */
}

/* --------------------------------------------------------------------------
 * 事件回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件事件回调，处理按压/滑动/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 返回状态标志（1 有效，0 无效）。
 */
static uint8_t _toggle_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_toggle_obj_t *obj = (we_toggle_obj_t *)ptr;

    /* 用户自定义回调优先，接管全部事件 */
    if (obj->user_event_cb != NULL)
        return obj->user_event_cb(ptr, event, data);

    (void)data;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        obj->pressed = 1U;
we_obj_invalidate((we_obj_t *)obj);
        break;
    case WE_EVENT_RELEASED:
        obj->pressed = 0U;
we_obj_invalidate((we_obj_t *)obj);
        break;
    case WE_EVENT_CLICKED:
we_toggle_toggle(obj);
        break;
    default:
        break;
    }
    return 1;
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param event_cb 回调函数指针。
 * @return 无。
 */
void we_toggle_obj_init(we_toggle_obj_t *obj, we_lcd_t *lcd,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        we_toggle_event_cb_t event_cb)
{
    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd     = lcd;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = w;
    obj->base.h       = h;
    obj->base.class_p = &_toggle_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;

    obj->user_event_cb = event_cb;
    obj->opacity       = 255U;
    obj->checked       = 0U;
    obj->pressed       = 0U;

#if WE_TOGGLE_USE_ANIM
    obj->anim_step = 0U; /* 初始 OFF，直接就位 */
    obj->anim_acc_ms = 0U;
    (void)_toggle_ensure_anim_task(lcd);
#endif

    /* 加入显示链表（追加到链尾，保持 Z 轴顺序） */
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
 * @brief 设置对象属性并同步刷新状态。
 * @param obj 目标控件对象指针。
 * @param checked 目标勾选状态（0 未选中，非 0 选中）。
 * @return 无。
 */
void we_toggle_set_checked(we_toggle_obj_t *obj, uint8_t checked)
{
    uint8_t val;

    if (obj == NULL)
        return;

    val = checked ? 1U : 0U;
    if (obj->checked == val)
        return;

    obj->checked = val;

    /* 立即同步动画步数，使视觉直接跳变到目标位置（无动画） */
#if WE_TOGGLE_USE_ANIM
    obj->anim_step = val ? (uint8_t)WE_TOGGLE_ANIM_STEPS : 0U;
    obj->anim_acc_ms = 0U;
#endif

we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 执行 we_toggle_toggle。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_toggle_toggle(we_toggle_obj_t *obj)
{
    if (obj == NULL)
        return;

    obj->checked = obj->checked ? 0U : 1U;
#if WE_TOGGLE_USE_ANIM
    obj->anim_acc_ms = 0U;
    if (_toggle_ensure_anim_task(obj->base.lcd) < 0)
    {
        obj->anim_step = obj->checked ? (uint8_t)WE_TOGGLE_ANIM_STEPS : 0U;
    }
#endif
    /* 不重置 anim_step，让动画从当前视觉位置自然过渡到新目标 */
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 执行 we_toggle_is_checked。
 * @param obj 目标控件对象指针。
 * @return 1 表示命中，0 表示未命中。
 */
uint8_t we_toggle_is_checked(const we_toggle_obj_t *obj)
{
    if (obj == NULL)
        return 0U;
    return obj->checked;
}

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_toggle_set_opacity(we_toggle_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
we_obj_invalidate((we_obj_t *)obj);
}
