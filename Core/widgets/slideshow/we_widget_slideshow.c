#include "we_widget_slideshow.h"
#include "we_render.h"

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _slideshow_draw_cb(void *ptr);

/**
 * @brief 控件事件回调，处理按压/滑动/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 返回状态标志（1 有效，0 无效）。
 */
static uint8_t _slideshow_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);

/**
 * @brief 周期任务回调，按时间步长推进本控件动画。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param user_data 任务回调用户数据指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _slideshow_task_cb(we_lcd_t *lcd, void *user_data, uint16_t elapsed_ms);

static const we_class_t _slideshow_class = { .draw_cb = _slideshow_draw_cb, .event_cb = _slideshow_event_cb };

/**
 * @brief 根据当前位置计算最近的分页吸附目标 X 坐标。
 * @param current 当前滚动 X 偏移。
 * @param page_w 单页宽度（像素）。
 * @param page_count 总页数。
 * @return 返回吸附目标坐标值。
 */
static int16_t _slideshow_find_snap_target(int16_t current, int16_t page_w, uint16_t page_count)
{
    int16_t best_snap = 0;
    int16_t min_diff = 32767;
    uint16_t i;

    if (page_count == 0U || page_w <= 0)
        return 0;

    for (i = 0; i < page_count; i++)
    {
        int16_t snap = (int16_t)(-(int16_t)i * page_w);
        int16_t diff = WE_ABS(current - snap);
        if (diff < min_diff)
        {
            min_diff = diff;
            best_snap = snap;
        }
    }

    return best_snap;
}

/**
 * @brief 按滑动方向查找下一页吸附目标 X 坐标。
 * @param current 当前滚动 X 偏移。
 * @param page_w 单页宽度（像素）。
 * @param page_count 总页数。
 * @param dir 翻页方向（+1 向左页，-1 向右页）。
 * @return 返回对应计算结果。
 */
static int16_t _slideshow_find_snap_dir(int16_t current, int16_t page_w, uint16_t page_count, int8_t dir)
{
    int16_t best = current;
    int16_t best_diff = 32767;
    uint16_t i;

    if (page_count == 0U || page_w <= 0)
        return current;

    for (i = 0; i < page_count; i++)
    {
        int16_t snap = (int16_t)(-(int16_t)i * page_w);
        int16_t diff = snap - current;
        if ((dir > 0 && diff > 0) || (dir < 0 && diff < 0))
        {
            int16_t abs_diff = (diff >= 0) ? diff : -diff;
            if (abs_diff < best_diff)
            {
                best_diff = abs_diff;
                best = snap;
            }
        }
    }

    return best;
}

/**
 * @brief 清空控件内部数据并重置游标。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _slideshow_clear_snap_state(we_slideshow_obj_t *obj)
{
    obj->snap_animating = 0;
#if (WE_SLIDESHOW_SNAP_ANIM_MODE == WE_SLIDESHOW_SNAP_ANIM_COMPLEX)
    obj->snap_vx = 0;
#endif
}

/**
 * @brief 更新属性并同步内部状态。
 * @param obj 目标控件对象指针。
 * @param target_x 吸附目标 X 坐标。
 * @return 无。
 */
static void _slideshow_set_snap_target(we_slideshow_obj_t *obj, int16_t target_x)
{
    obj->snap_target_x = target_x;
    obj->snap_animating = (target_x != obj->scroll_x);
#if (WE_SLIDESHOW_SNAP_ANIM_MODE == WE_SLIDESHOW_SNAP_ANIM_COMPLEX)
    obj->snap_vx = 0;
#endif
}

/**
 * @brief 按帧间隔缩放基准步进，确保最小步进为 1 像素。
 * @param base_step 以 16ms 为基准的步进值。
 * @param step_ms 动画步长时间（毫秒）。
 * @return 返回对应计算结果。
 */
static int16_t _slideshow_scaled_step(uint16_t base_step, uint16_t step_ms)
{
    int16_t step = (int16_t)(((uint32_t)base_step * step_ms + 15U) / 16U);
    return (step <= 0) ? 1 : step;
}

#if (WE_SLIDESHOW_SNAP_ANIM_MODE == WE_SLIDESHOW_SNAP_ANIM_SIMPLE)

/**
 * @brief 计算简单吸附模式下单轴本帧位移。
 * @param diff 当前位置与目标位置的轴向差值。
 * @param step_ms 动画步长时间（毫秒）。
 * @return 返回对应计算结果。
 */
static int16_t _slideshow_anim_step_simple_axis(int16_t diff, uint16_t step_ms)
{
    int16_t step;

    if (diff == 0)
        return 0;

    step = _slideshow_scaled_step(WE_SLIDESHOW_SNAP_SIMPLE_STEP, step_ms);
    if (WE_ABS(diff) <= step)
        return diff;
    return (diff > 0) ? step : -step;
}
#elif (WE_SLIDESHOW_SNAP_ANIM_MODE == WE_SLIDESHOW_SNAP_ANIM_COMPLEX)

/**
 * @brief 计算阻尼吸附模式下单轴本帧位移。
 * @param diff 当前位置与目标位置的轴向差值。
 * @param velocity 当前轴向速度累积值指针。
 * @param step_ms 动画步长时间（毫秒）。
 * @return 返回对应计算结果。
 */
static int16_t _slideshow_anim_step_complex_axis(int16_t diff, int16_t *velocity, uint16_t step_ms)
{
    int16_t pull = diff / WE_SLIDESHOW_SNAP_COMPLEX_PULL_DIV;
    int16_t move;
    int16_t max_step;

    if (diff == 0)
    {
        *velocity = 0;
        return 0;
    }

    if (pull != 0)
    {
        pull = (int16_t)((pull * (int32_t)step_ms + 15) / 16);
        if (pull == 0)
            pull = (diff > 0) ? 1 : -1;
    }

    *velocity += pull;
    *velocity = (int16_t)((*velocity * WE_SLIDESHOW_SNAP_COMPLEX_DAMP_NUM) / WE_SLIDESHOW_SNAP_COMPLEX_DAMP_DEN);
    move = *velocity;
    if (move == 0)
        move = (diff > 0) ? 1 : -1;

    max_step = _slideshow_scaled_step(WE_SLIDESHOW_SNAP_COMPLEX_MAX_STEP, step_ms);
    if (move > max_step)
        move = max_step;
    if (move < -max_step)
        move = -max_step;
    if (WE_ABS(move) > WE_ABS(diff))
        move = diff;
    return move;
}
#endif

/**
 * @brief 计算分页吸附目标并更新动画参数。
 * @param obj 目标控件对象指针。
 * @param snap_x X 方向坐标或偏移值。
 * @return 返回对应计算结果。
 */
static uint16_t _slideshow_snap_to_page_index(const we_slideshow_obj_t *obj, int16_t snap_x)
{
    uint16_t i;

    if (obj == NULL || obj->group.base.w <= 0 || obj->page_count == 0U)
        return 0U;

    for (i = 0; i < obj->page_count; i++)
    {
        if (snap_x == (int16_t)(-(int16_t)i * obj->group.base.w))
            return i;
    }

    return 0U;
}

static we_slideshow_child_slot_t *_slideshow_find_slot(we_slideshow_obj_t *obj, we_obj_t *child)
{
    uint16_t i;

    if (obj == NULL || child == NULL)
        return NULL;

    for (i = 0; i < WE_SLIDESHOW_CHILD_MAX; i++)
    {
        if (obj->child_slots[i].used && obj->child_slots[i].child == child)
            return &obj->child_slots[i];
    }

    return NULL;
}

/**
 * @brief 根据页索引与滚动偏移更新子控件绝对坐标。
 * @param obj 目标控件对象指针。
 * @param slot 子控件槽位指针。
 * @return 无。
 */
static void _slideshow_update_child_page_abs(we_slideshow_obj_t *obj, we_slideshow_child_slot_t *slot)
{
    if (obj == NULL || slot == NULL || !slot->used || slot->child == NULL)
        return;

    we_group_set_child_pos(&obj->group, slot->child,
                           (int16_t)((int16_t)(slot->page_index * obj->group.base.w) + obj->scroll_x + slot->local_x),
                           slot->local_y);
}

/**
 * @brief 更新属性并同步内部状态。
 * @param obj 目标控件对象指针。
 * @param dx 滚动增量 X（像素）。
 * @return 无。
 */
static void _slideshow_set_scroll(we_slideshow_obj_t *obj, int16_t dx)
{
    int16_t min_scroll;
    int16_t max_scroll;

    if (obj == NULL || dx == 0)
        return;

    max_scroll = 0;
    min_scroll = (obj->page_count > 0U) ? (int16_t)(-(int16_t)(obj->page_count - 1U) * obj->group.base.w) : 0;

    if (obj->scroll_x + dx > max_scroll)
        dx = max_scroll - obj->scroll_x;
    if (obj->scroll_x + dx < min_scroll)
        dx = min_scroll - obj->scroll_x;
    if (dx == 0)
        return;

    obj->scroll_x += dx;
    we_group_shift_children(&obj->group, dx, 0, 1U);
}

/**
 * @brief 计算分页吸附目标并更新动画参数。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _slideshow_begin_snap(we_slideshow_obj_t *obj)
{
    int16_t target_x;

    if (obj == NULL)
        return;

    target_x = _slideshow_find_snap_target(obj->scroll_x, obj->group.base.w, obj->page_count);
#if (WE_SLIDESHOW_SNAP_ANIM_MODE == WE_SLIDESHOW_SNAP_ANIM_NONE)
    _slideshow_set_scroll(obj, target_x - obj->scroll_x);
    obj->current_page = _slideshow_snap_to_page_index(obj, obj->scroll_x);
#else
    _slideshow_set_snap_target(obj, target_x);
#endif
}

/**
 * @brief 处理滑动手势并计算目标页吸附位置。
 * @param obj 目标控件对象指针。
 * @param event 输入事件类型。
 * @return 无。
 */
static void _slideshow_handle_swipe(we_slideshow_obj_t *obj, we_event_t event)
{
    int16_t target_x = obj->scroll_x;

    if (obj == NULL || obj->page_count == 0U)
        return;

    if (event == WE_EVENT_SWIPE_LEFT)
        target_x = _slideshow_find_snap_dir(obj->scroll_x, obj->group.base.w, obj->page_count, +1);
    else if (event == WE_EVENT_SWIPE_RIGHT)
        target_x = _slideshow_find_snap_dir(obj->scroll_x, obj->group.base.w, obj->page_count, -1);

    if (target_x == obj->scroll_x)
        return;

#if (WE_SLIDESHOW_SNAP_ANIM_MODE == WE_SLIDESHOW_SNAP_ANIM_NONE)
    _slideshow_set_scroll(obj, target_x - obj->scroll_x);
    obj->current_page = _slideshow_snap_to_page_index(obj, obj->scroll_x);
#else
    _slideshow_set_snap_target(obj, target_x);
#endif
}

/**
 * @brief 推进一次吸附动画状态。
 * @param obj 目标控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _slideshow_anim_step(we_slideshow_obj_t *obj, uint16_t elapsed_ms)
{
#if (WE_SLIDESHOW_SNAP_ANIM_MODE != WE_SLIDESHOW_SNAP_ANIM_NONE)
    int16_t diff_x;
    int16_t move_x = 0;
    uint16_t step_ms;

    if (obj == NULL || !obj->snap_animating || obj->is_dragging || elapsed_ms == 0U)
        return;

    step_ms = (elapsed_ms > 64U) ? 64U : elapsed_ms;
    diff_x = obj->snap_target_x - obj->scroll_x;

#if (WE_SLIDESHOW_SNAP_ANIM_MODE == WE_SLIDESHOW_SNAP_ANIM_SIMPLE)
    move_x = _slideshow_anim_step_simple_axis(diff_x, step_ms);
#elif (WE_SLIDESHOW_SNAP_ANIM_MODE == WE_SLIDESHOW_SNAP_ANIM_COMPLEX)
    move_x = _slideshow_anim_step_complex_axis(diff_x, &obj->snap_vx, step_ms);
#endif

    if (move_x != 0)
        _slideshow_set_scroll(obj, move_x);

    if (obj->scroll_x == obj->snap_target_x)
    {
        obj->current_page = _slideshow_snap_to_page_index(obj, obj->scroll_x);
        _slideshow_clear_snap_state(obj);
    }
#else
    (void)obj;
    (void)elapsed_ms;
#endif
}

/**
 * @brief 周期任务回调，按时间步长推进本控件动画。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param user_data 任务回调用户数据指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _slideshow_task_cb(we_lcd_t *lcd, void *user_data, uint16_t elapsed_ms)
{
    we_slideshow_obj_t *obj = (we_slideshow_obj_t *)user_data;
    (void)lcd;
    if (obj == NULL)
        return;
    _slideshow_anim_step(obj, elapsed_ms);
}

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _slideshow_draw_cb(void *ptr)
{
    we_slideshow_obj_t *obj = (we_slideshow_obj_t *)ptr;
    we_lcd_t *lcd = obj->group.base.lcd;

    if (obj->group.opacity == 0)
        return;

    we_draw_round_rect(lcd, obj->group.base.x, obj->group.base.y,
                       obj->group.base.w, obj->group.base.h,
                       0, obj->group.bg_color, obj->group.opacity);

    {
        we_area_t old_pfb_area = lcd->pfb_area;
        uint16_t old_y_start = lcd->pfb_y_start;
        uint16_t old_y_end = lcd->pfb_y_end;
        colour_t *old_gram = lcd->pfb_gram;
        int16_t new_x0 = WE_MAX(old_pfb_area.x0, obj->group.base.x);
        int16_t new_y0 = WE_MAX(old_y_start, obj->group.base.y);
        int16_t new_x1 = WE_MIN(old_pfb_area.x1, obj->group.base.x + obj->group.base.w - 1);
        int16_t new_y1 = WE_MIN(old_y_end, obj->group.base.y + obj->group.base.h - 1);

        if (new_x0 <= new_x1 && new_y0 <= new_y1)
        {
            we_obj_t *child = obj->group.children_head;
            lcd->pfb_area.x0 = new_x0;
            lcd->pfb_area.x1 = new_x1;
            lcd->pfb_y_start = new_y0;
            lcd->pfb_y_end = new_y1;
            lcd->pfb_gram = old_gram + (new_y0 - old_y_start) * lcd->pfb_width + (new_x0 - old_pfb_area.x0);

            while (child != NULL)
            {
                if (child->class_p && child->class_p->draw_cb)
                    child->class_p->draw_cb(child);
                child = child->next;
            }
        }

        lcd->pfb_area = old_pfb_area;
        lcd->pfb_y_start = old_y_start;
        lcd->pfb_y_end = old_y_end;
        lcd->pfb_gram = old_gram;
    }
}

/**
 * @brief 控件事件回调，处理按压/滑动/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 返回状态标志（1 有效，0 无效）。
 */
static uint8_t _slideshow_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_slideshow_obj_t *obj = (we_slideshow_obj_t *)ptr;

    if (!data)
        return 0;

    if (event == WE_EVENT_PRESSED)
    {
        we_obj_t *target_child = NULL;
        we_obj_t *curr = obj->group.children_head;

        _slideshow_clear_snap_state(obj);

        while (curr != NULL)
        {
            if (curr->class_p && curr->class_p->event_cb)
            {
                if (data->x >= curr->x && data->x < (curr->x + curr->w) && data->y >= curr->y &&
                    data->y < (curr->y + curr->h))
                {
                    if (curr->class_p->event_cb(curr, event, data) == 1)
                    {
                        target_child = curr;
                        break;
                    }
                }
            }
            curr = curr->next;
        }

        if (target_child)
        {
            obj->last_pressed_child = target_child;
            obj->is_dragging = 0;
            return 1;
        }

        obj->last_pressed_child = NULL;
        obj->is_dragging = 1;
        obj->last_touch_x = data->x;
        obj->last_touch_y = data->y;
        return 1;
    }

    if (obj->last_pressed_child != NULL)
    {
        if (event == WE_EVENT_CLICKED)
        {
            we_obj_t *child = obj->last_pressed_child;
            if (!(data->x >= child->x && data->x < (child->x + child->w) && data->y >= child->y &&
                  data->y < (child->y + child->h)))
            {
                obj->last_pressed_child = NULL;
                return 1;
            }
        }

        obj->last_pressed_child->class_p->event_cb(obj->last_pressed_child, event, data);
        if (event == WE_EVENT_CLICKED)
            obj->last_pressed_child = NULL;
        return 1;
    }

    if (event == WE_EVENT_STAY && obj->is_dragging)
    {
        int16_t dx = data->x - obj->last_touch_x;
        if (dx != 0)
        {
            _slideshow_set_scroll(obj, dx);
            obj->last_touch_x = data->x;
            obj->last_touch_y = data->y;
        }
    }
    else if (event == WE_EVENT_RELEASED)
    {
        if (obj->is_dragging)
            _slideshow_begin_snap(obj);
        obj->is_dragging = 0;
    }
    else if (obj->swipe_enabled && (event == WE_EVENT_SWIPE_LEFT || event == WE_EVENT_SWIPE_RIGHT))
    {
        _slideshow_handle_swipe(obj, event);
        obj->is_dragging = 0;
    }

    return 1;
}

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param bg_color 背景颜色值。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_slideshow_obj_init(we_slideshow_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y, int16_t w, int16_t h,
                           colour_t bg_color, uint8_t opacity)
{
    uint16_t i;

    if (obj == NULL || lcd == NULL)
        return;

    we_group_obj_init(&obj->group, lcd, x, y, w, h, bg_color, opacity);
    obj->group.base.class_p = &_slideshow_class;
    obj->last_pressed_child = NULL;
    obj->scroll_x = 0;
    obj->last_touch_x = 0;
    obj->last_touch_y = 0;
    obj->snap_target_x = 0;
#if (WE_SLIDESHOW_SNAP_ANIM_MODE == WE_SLIDESHOW_SNAP_ANIM_COMPLEX)
    obj->snap_vx = 0;
#endif
    obj->page_count = 0U;
    obj->current_page = 0U;
    obj->swipe_enabled = 1U;
    obj->is_dragging = 0U;
    obj->snap_animating = 0U;

    for (i = 0; i < WE_SLIDESHOW_CHILD_MAX; i++)
        obj->child_slots[i].used = 0U;

    obj->group.task_id = _we_gui_task_register_with_data(lcd, _slideshow_task_cb, obj);
}

/**
 * @brief 释放控件运行时状态并从任务系统注销。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_slideshow_obj_delete(we_slideshow_obj_t *obj)
{
    uint16_t i;

    if (obj == NULL || obj->group.base.lcd == NULL)
        return;

    obj->last_pressed_child = NULL;
    for (i = 0; i < WE_SLIDESHOW_CHILD_MAX; i++)
        obj->child_slots[i].used = 0U;

    if (obj->group.task_id >= 0)
    {
        _we_gui_task_unregister(obj->group.base.lcd, obj->group.task_id);
        obj->group.task_id = -1;
    }

    we_group_obj_delete(&obj->group);
}

/**
 * @brief 新增一页并返回其页索引。
 * @param obj 目标控件对象指针。
 * @return 返回对应计算结果。
 */
uint16_t we_slideshow_add_page(we_slideshow_obj_t *obj)
{
    if (obj == NULL || obj->page_count >= WE_SLIDESHOW_PAGE_MAX)
        return 0xFFFFU;
    return obj->page_count++;
}

/**
 * @brief 读取当前总页数。
 * @param obj 目标控件对象指针。
 * @return 返回对应计算结果。
 */
uint16_t we_slideshow_get_page_count(const we_slideshow_obj_t *obj)
{
    return (obj == NULL) ? 0U : obj->page_count;
}

/**
 * @brief 读取当前页索引。
 * @param obj 目标控件对象指针。
 * @return 返回对应计算结果。
 */
uint16_t we_slideshow_get_current_page(const we_slideshow_obj_t *obj)
{
    return (obj == NULL) ? 0U : obj->current_page;
}

/**
 * @brief 将子控件挂载到指定页面。
 * @param obj 目标控件对象指针。
 * @param page_index 目标页索引。
 * @param child 待挂载的子控件对象指针。
 * @return 无。
 */
void we_slideshow_add_child(we_slideshow_obj_t *obj, uint16_t page_index, we_obj_t *child)
{
    uint16_t i;

    if (obj == NULL || child == NULL)
        return;
    if (child == (we_obj_t *)obj)
        return;
    if (child->lcd != obj->group.base.lcd)
        return;
    if (page_index >= obj->page_count)
        return;
    if (_slideshow_find_slot(obj, child) != NULL)
        return;

    for (i = 0; i < WE_SLIDESHOW_CHILD_MAX; i++)
    {
        if (!obj->child_slots[i].used)
        {
            we_group_add_child(&obj->group, child);
            obj->child_slots[i].child = child;
            obj->child_slots[i].page_index = page_index;
            obj->child_slots[i].local_x = 0;
            obj->child_slots[i].local_y = 0;
            obj->child_slots[i].used = 1U;
            _slideshow_update_child_page_abs(obj, &obj->child_slots[i]);
            return;
        }
    }
}

/**
 * @brief 更新属性并同步内部状态。
 * @param obj 目标控件对象指针。
 * @param child 待挂载的子控件对象指针。
 * @param local_x X 方向坐标或偏移值。
 * @param local_y Y 方向坐标或偏移值。
 * @return 无。
 */
void we_slideshow_set_child_pos(we_slideshow_obj_t *obj, we_obj_t *child, int16_t local_x, int16_t local_y)
{
    we_slideshow_child_slot_t *slot = _slideshow_find_slot(obj, child);

    if (slot == NULL)
        return;

    slot->local_x = local_x;
    slot->local_y = local_y;
    _slideshow_update_child_page_abs(obj, slot);
}

/**
 * @brief 切换到指定页面并处理吸附动画。
 * @param obj 目标控件对象指针。
 * @param page_index 目标页索引。
 * @param animated 是否启用动画（0 否，非 0 是）。
 * @return 无。
 */
void we_slideshow_set_page(we_slideshow_obj_t *obj, uint16_t page_index, uint8_t animated)
{
    int16_t target_x;

    if (obj == NULL || obj->page_count == 0U)
        return;
    if (page_index >= obj->page_count)
        page_index = (uint16_t)(obj->page_count - 1U);

    target_x = (int16_t)(-(int16_t)page_index * obj->group.base.w);

    if (!animated || WE_SLIDESHOW_SNAP_ANIM_MODE == WE_SLIDESHOW_SNAP_ANIM_NONE)
    {
        _slideshow_clear_snap_state(obj);
        _slideshow_set_scroll(obj, target_x - obj->scroll_x);
        obj->current_page = page_index;
        return;
    }

    obj->current_page = page_index;
    _slideshow_set_snap_target(obj, target_x);
}

/**
 * @brief 切换到下一页并更新当前页索引。
 * @param obj 目标控件对象指针。
 * @param animated 是否启用动画（0 否，非 0 是）。
 * @return 无。
 */
void we_slideshow_next(we_slideshow_obj_t *obj, uint8_t animated)
{
    uint16_t next_page;

    if (obj == NULL || obj->page_count == 0U)
        return;

    next_page = (obj->current_page + 1U < obj->page_count) ? (uint16_t)(obj->current_page + 1U) : obj->current_page;
    we_slideshow_set_page(obj, next_page, animated);
}

/**
 * @brief 切换到上一页并更新当前页索引。
 * @param obj 目标控件对象指针。
 * @param animated 是否启用动画（0 否，非 0 是）。
 * @return 无。
 */
void we_slideshow_prev(we_slideshow_obj_t *obj, uint8_t animated)
{
    uint16_t prev_page;

    if (obj == NULL || obj->page_count == 0U)
        return;

    prev_page = (obj->current_page > 0U) ? (uint16_t)(obj->current_page - 1U) : 0U;
    we_slideshow_set_page(obj, prev_page, animated);
}

/**
 * @brief 更新属性并同步内部状态。
 * @param obj 目标控件对象指针。
 * @param en 使能开关（0 关闭，非 0 开启）。
 * @return 无。
 */
void we_slideshow_set_swipe_enabled(we_slideshow_obj_t *obj, uint8_t en)
{
    if (obj == NULL)
        return;
    obj->swipe_enabled = (en != 0U) ? 1U : 0U;
}

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_slideshow_set_opacity(we_slideshow_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL)
        return;
    we_group_set_opacity(&obj->group, opacity);
}
