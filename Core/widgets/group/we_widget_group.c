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
 * @brief 把对象从其当前所属链表（父容器 children_head 或顶层 obj_list_head）摘除，并清空 next/parent，供改挂父子关系前使用。
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
 * @brief 按 slot 局部坐标叠加容器绝对坐标，刷新该子控件的屏幕绝对位置。
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

we_draw_round_rect_analytic_fill(lcd, obj->base.x, obj->base.y,
                                     (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                     0U, obj->bg_color, obj->opacity);

    {
        we_area_t old_pfb_area = lcd->pfb_area;
        uint16_t old_y_start = lcd->pfb_y_start;
        uint16_t old_y_end = lcd->pfb_y_end;
        colour_t *old_gram = lcd->pfb_gram;
        uint8_t old_scale = lcd->opa_scale;

        /* 子控件透明度级联：把本容器 opacity 乘进全局乘子（嵌套自动链乘） */
        lcd->opa_scale = we_opa_apply(lcd, obj->opacity);
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
                if (child->class_p && child->class_p->draw_cb &&
                    (child->x + child->w > lcd->pfb_area.x0) && (child->x <= lcd->pfb_area.x1) &&
                    (child->y + child->h > lcd->pfb_y_start) && (child->y <= lcd->pfb_y_end))
                {
                    child->class_p->draw_cb(child);
                }
                child = child->next;
            }
        }

        lcd->opa_scale = old_scale;
        lcd->pfb_area = old_pfb_area;
        lcd->pfb_y_start = old_y_start;
        lcd->pfb_y_end = old_y_end;
        lcd->pfb_gram = old_gram;
    }
}

/* --------------------------------------------------------------------------
 * 命中转发：核心输入分发只扫顶层链表，不递归 children_head。
 * group 通过自身 event_cb 把触摸事件转发给命中的子控件，
 * 否则放进裸 group 的交互控件（btn/checkbox/...）永远收不到输入。
 * 子控件存绝对坐标，直接矩形命中即可；后挂的子控件层级更高，取最后命中者。
 * -------------------------------------------------------------------------- */

/**
 * @brief 在组内查找命中坐标的可交互子控件。
 * @param obj 目标控件对象指针。
 * @param x 屏幕绝对 X 坐标。
 * @param y 屏幕绝对 Y 坐标。
 * @return 命中的子控件指针；无命中返回 NULL。
 */
static we_obj_t *_group_hit_child(we_group_obj_t *obj, int16_t x, int16_t y)
{
    we_obj_t *child = obj->children_head;
    we_obj_t *target = NULL;

    while (child != NULL)
    {
        if (child->class_p != NULL && child->class_p->event_cb != NULL &&
            x >= child->x && x < (child->x + child->w) &&
            y >= child->y && y < (child->y + child->h))
        {
            target = child;
        }
        child = child->next;
    }
    return target;
}

/**
 * @brief 组容器事件回调：按压时锁定子控件，后续事件按序转发。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 已处理，0 未处理。
 */
static uint8_t _group_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_group_obj_t *obj = (we_group_obj_t *)ptr;
    we_obj_t *child;

    if (obj == NULL || data == NULL || obj->opacity == 0U)
        return 0U; /* 完全透明（淡出隐藏）的容器不拦截输入 */

    if (event == WE_EVENT_PRESSED)
    {
        child = _group_hit_child(obj, data->x, data->y);
        obj->last_pressed_child = child;
        if (child != NULL)
        {
            child->class_p->event_cb(child, WE_EVENT_PRESSED, data);
            return 1U;
        }
        /* 未命中交互子控件时返回 0：让外层容器（如 slideshow）
         * 把这次按压用于拖拽翻页，group 空白区不吞手势。 */
        return 0U;
    }

    /* 仅转发触摸序列事件；SCROLLED 等广播事件不属于转发范围 */
    if (event != WE_EVENT_RELEASED && event != WE_EVENT_STAY && event != WE_EVENT_CLICKED &&
        event != WE_EVENT_SWIPE_LEFT && event != WE_EVENT_SWIPE_RIGHT &&
        event != WE_EVENT_SWIPE_UP && event != WE_EVENT_SWIPE_DOWN)
        return 0U;

    child = obj->last_pressed_child;
    if (child == NULL)
        return 0U;
    if (child->class_p == NULL || child->class_p->event_cb == NULL)
    {
        /* 子控件已在按压期间被删除/失效，丢弃引用 */
        obj->last_pressed_child = NULL;
        return 0U;
    }

    if (event == WE_EVENT_CLICKED)
    {
        /* 点击需复核释放点仍落在原子控件上，按下后拖出再松手不触发 */
        if (data->x >= child->x && data->x < (child->x + child->w) &&
            data->y >= child->y && data->y < (child->y + child->h))
            child->class_p->event_cb(child, WE_EVENT_CLICKED, data);
        obj->last_pressed_child = NULL;
        return 1U;
    }

    child->class_p->event_cb(child, event, data);
    return 1U;
}

/**
 * @brief 容器移动回调：平移外框的同时按局部坐标同步全部子控件。
 * @param ptr 回调透传对象指针。
 * @param new_x 新的左上角 X 坐标。
 * @param new_y 新的左上角 Y 坐标。
 * @return 无。
 * @note 没有它时，外层容器（如 slideshow 翻页）经 we_obj_set_pos 移动
 *       group 只会挪外框，子控件会留在原地。子控件经 we_obj_set_pos
 *       跟随，嵌套 group / 带 set_pos_cb 的控件（img_ex 等）自动级联。
 */
static void _group_set_pos_cb(void *ptr, int16_t new_x, int16_t new_y)
{
    we_group_obj_t *obj = (we_group_obj_t *)ptr;

    we_obj_invalidate((we_obj_t *)obj);
    obj->base.x = new_x;
    obj->base.y = new_y;
    we_group_relayout(obj); /* 按 slot 局部坐标刷新全部子控件绝对位置 */
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 初始化组容器对象（清空子控件槽、挂载到 LCD 链表）。
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
    static const we_class_t _group_class = { .draw_cb = _group_draw_cb, .event_cb = _group_event_cb, .set_pos_cb = _group_set_pos_cb};
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
    obj->children_head = NULL;
    obj->bg_color = bg_color;
    obj->opacity = opacity;
    obj->last_pressed_child = NULL;

    for (i = 0; i < WE_GROUP_CHILD_MAX; i++)
        obj->child_slots[i].used = 0U;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

    if (opacity > 0U)
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除组容器：先逐个删除全部子控件并清空 slot，再删除容器自身。
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
 * @brief 将子控件从原链表摘出并挂入本组的空闲 slot（建立父子关系、刷新绝对坐标）；自挂载/跨 lcd/重复挂载会被忽略。
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

            we_obj_append_to_list(&obj->children_head, child);

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
 * @brief 从组中移除子控件：去链并释放其 slot；若它正处于按压转发状态则同步清除引用。
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

    /* 被移除的子控件若正处于按压转发状态，同步丢弃引用 */
    if (obj->last_pressed_child == child)
        obj->last_pressed_child = NULL;
}

/**
 * @brief 设置子控件在组内的局部坐标并刷新其屏幕绝对位置。
 * @param obj 目标控件对象指针。
 * @param child 目标子控件对象指针。
 * @param local_x 相对组左上角的局部 X 坐标（像素）。
 * @param local_y 相对组左上角的局部 Y 坐标（像素）。
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
 * @brief 按各 slot 局部坐标重新刷新全部子控件的屏幕绝对位置。
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
 * @brief 按给定位移整体平移全部子控件，并可选派发 WE_EVENT_SCROLLED。
 * @param obj 目标控件对象指针。
 * @param dx X 方向平移增量（像素）。
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
