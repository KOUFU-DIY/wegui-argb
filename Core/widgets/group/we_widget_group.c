#include "we_widget_group.h"
#include "we_render.h"

static we_group_child_slot_t *_group_find_slot(we_group_obj_t *obj, we_obj_t *child)
{
    uint16_t i;

    if (obj == NULL || child == NULL)
        return NULL;

    for (i = 0; i < WE_GROUP_CHILD_MAX; i++)
    {
        if (obj->child_slots[i].used && obj->child_slots[i].child == child)
            return &obj->child_slots[i];
    }

    return NULL;
}

/**
 * @brief 内部辅助：group_detach_obj。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _group_detach_obj(we_obj_t *obj)
{
    we_obj_t *curr;
    we_obj_t *prev;

    if (obj == NULL || obj->lcd == NULL)
        return;

    if (obj->parent != NULL)
    {
        we_child_owner_t *parent = (we_child_owner_t *)obj->parent;
        curr = parent->children_head;
        prev = NULL;
        while (curr != NULL)
        {
            if (curr == obj)
            {
                if (prev == NULL)
                    parent->children_head = curr->next;
                else
                    prev->next = curr->next;
                break;
            }
            prev = curr;
            curr = curr->next;
        }
    }
    else
    {
        curr = obj->lcd->obj_list_head;
        prev = NULL;
        while (curr != NULL)
        {
            if (curr == obj)
            {
                if (prev == NULL)
                    obj->lcd->obj_list_head = curr->next;
                else
                    prev->next = curr->next;
                break;
            }
            prev = curr;
            curr = curr->next;
        }
    }

    obj->next = NULL;
    obj->parent = NULL;
}

/**
 * @brief 内部辅助：group_update_child_abs。
 * @param obj 目标控件对象指针。
 * @param slot 子控件槽位记录指针。
 * @return 无。
 */
static void _group_update_child_abs(we_group_obj_t *obj, we_group_child_slot_t *slot)
{
    if (obj == NULL || slot == NULL || !slot->used || slot->child == NULL)
        return;

    we_obj_set_pos(slot->child,
                   (int16_t)(obj->base.x + slot->local_x),
                   (int16_t)(obj->base.y + slot->local_y));
}

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _group_draw_cb(void *ptr)
{
    we_group_obj_t *obj = (we_group_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;

    if (obj->opacity == 0)
        return;

we_draw_round_rect(lcd, obj->base.x, obj->base.y, obj->base.w, obj->base.h, 0, obj->bg_color, obj->opacity);

    {
        we_area_t old_pfb_area = lcd->pfb_area;
        uint16_t old_y_start = lcd->pfb_y_start;
        uint16_t old_y_end = lcd->pfb_y_end;
        colour_t *old_gram = lcd->pfb_gram;
int16_t new_x0 = WE_MAX(old_pfb_area.x0, obj->base.x);
int16_t new_y0 = WE_MAX(old_y_start, obj->base.y);
int16_t new_x1 = WE_MIN(old_pfb_area.x1, obj->base.x + obj->base.w - 1);
int16_t new_y1 = WE_MIN(old_y_end, obj->base.y + obj->base.h - 1);

        if (new_x0 <= new_x1 && new_y0 <= new_y1)
        {
            we_obj_t *child = obj->children_head;
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
void we_group_obj_init(we_group_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y, int16_t w, int16_t h,
                       colour_t bg_color, uint8_t opacity)
{
    static const we_class_t _group_class = { .draw_cb = _group_draw_cb, .event_cb = NULL };
    uint16_t i;

    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_group_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;
    obj->task_id = -1;
    obj->children_head = NULL;
    obj->bg_color = bg_color;
    obj->opacity = opacity;

    for (i = 0; i < WE_GROUP_CHILD_MAX; i++)
        obj->child_slots[i].used = 0U;

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

    if (opacity > 0U)
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 释放控件运行时状态并从任务系统注销。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_group_obj_delete(we_group_obj_t *obj)
{
    we_obj_t *child;
    we_obj_t *next;
    uint16_t i;

    if (obj == NULL || obj->base.lcd == NULL)
        return;

    child = obj->children_head;
    while (child != NULL)
    {
        next = child->next;
we_obj_delete(child);
        child = next;
    }

    obj->children_head = NULL;
    for (i = 0; i < WE_GROUP_CHILD_MAX; i++)
        obj->child_slots[i].used = 0U;

we_obj_delete((we_obj_t *)obj);
}

/**
 * @brief 执行 we_group_add_child。
 * @param obj 目标控件对象指针。
 * @param child 目标子控件对象指针。
 * @return 无。
 */
void we_group_add_child(we_group_obj_t *obj, we_obj_t *child)
{
    uint16_t i;

    if (obj == NULL || child == NULL)
        return;
    if (child == (we_obj_t *)obj)
        return;
    if (child->lcd != obj->base.lcd)
        return;
    if (_group_find_slot(obj, child) != NULL)
        return;

    for (i = 0; i < WE_GROUP_CHILD_MAX; i++)
    {
        if (!obj->child_slots[i].used)
        {
_group_detach_obj(child);
            child->next = NULL;
            child->parent = (we_obj_t *)obj;

            if (obj->children_head == NULL)
            {
                obj->children_head = child;
            }
            else
            {
                we_obj_t *tail = obj->children_head;
                while (tail->next != NULL)
                    tail = tail->next;
                tail->next = child;
            }

            obj->child_slots[i].child = child;
            obj->child_slots[i].local_x = 0;
            obj->child_slots[i].local_y = 0;
            obj->child_slots[i].used = 1U;
_group_update_child_abs(obj, &obj->child_slots[i]);
            return;
        }
    }
}

/**
 * @brief 执行 we_group_remove_child。
 * @param obj 目标控件对象指针。
 * @param child 目标子控件对象指针。
 * @return 无。
 */
void we_group_remove_child(we_group_obj_t *obj, we_obj_t *child)
{
we_group_child_slot_t *slot = _group_find_slot(obj, child);

    if (slot == NULL)
        return;

_group_detach_obj(child);
    slot->used = 0U;
    slot->child = NULL;
}

/**
 * @brief 设置对象属性并同步刷新状态。
 * @param obj 目标控件对象指针。
 * @param child 目标子控件对象指针。
 * @param local_x X 方向坐标或偏移值。
 * @param local_y Y 方向坐标或偏移值。
 * @return 无。
 */
void we_group_set_child_pos(we_group_obj_t *obj, we_obj_t *child, int16_t local_x, int16_t local_y)
{
we_group_child_slot_t *slot = _group_find_slot(obj, child);

    if (slot == NULL)
        return;

    slot->local_x = local_x;
    slot->local_y = local_y;
_group_update_child_abs(obj, slot);
}

/**
 * @brief 执行 we_group_relayout。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_group_relayout(we_group_obj_t *obj)
{
    uint16_t i;

    if (obj == NULL)
        return;

    for (i = 0; i < WE_GROUP_CHILD_MAX; i++)
    {
        if (obj->child_slots[i].used)
_group_update_child_abs(obj, &obj->child_slots[i]);
    }
}

/**
 * @brief 执行 we_group_shift_children。
 * @param obj 目标控件对象指针。
 * @param dx 滚动增量 X（像素）。
 * @param dy Y 方向平移增量（像素）。
 * @param send_scrolled_event 是否向子控件派发 WE_EVENT_SCROLLED 事件。
 * @return 无。
 */
void we_group_shift_children(we_group_obj_t *obj, int16_t dx, int16_t dy, uint8_t send_scrolled_event)
{
    we_obj_t *child;
    we_indev_data_t scroll_data = {0};

    if (obj == NULL || (dx == 0 && dy == 0))
        return;

    scroll_data.x = dx;
    scroll_data.y = dy;

    child = obj->children_head;
    while (child != NULL)
    {
we_obj_set_pos(child, child->x + dx, child->y + dy);
        if (send_scrolled_event && child->class_p && child->class_p->event_cb)
            child->class_p->event_cb(child, WE_EVENT_SCROLLED, &scroll_data);
        child = child->next;
    }
}

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_group_set_opacity(we_group_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;

    if (obj->opacity > 0U)
we_obj_invalidate((we_obj_t *)obj);
    obj->opacity = opacity;
    if (obj->opacity > 0U)
we_obj_invalidate((we_obj_t *)obj);
}
