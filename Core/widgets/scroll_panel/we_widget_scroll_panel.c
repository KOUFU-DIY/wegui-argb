#include "we_widget_scroll_panel.h"
#include "we_render.h"

static int16_t _scroll_panel_max_scroll_y(const we_scroll_panel_obj_t *obj);

static int16_t _scroll_panel_clamp_to_bounds(we_scroll_panel_obj_t *obj, int16_t scroll_y)
{
    int16_t max_scroll;

    if (obj == NULL)
        return scroll_y;

    max_scroll = _scroll_panel_max_scroll_y(obj);
    if (scroll_y < 0)
        scroll_y = 0;
    if (scroll_y > max_scroll)
        scroll_y = max_scroll;
    return scroll_y;
}

static void _scroll_panel_update_inner_rect(we_scroll_panel_obj_t *obj)
{
    if (obj == NULL)
        return;

    obj->inner_x = (int16_t)(obj->base.x + 1);
    obj->inner_y = (int16_t)(obj->base.y + 1);
    obj->inner_w = (int16_t)(obj->base.w - 2);
    obj->inner_h = (int16_t)(obj->base.h - 2);

    if (obj->inner_w < 0)
        obj->inner_w = 0;
    if (obj->inner_h < 0)
        obj->inner_h = 0;
}

static int16_t _scroll_panel_clamp_overscroll(int16_t scroll_y, int16_t max_scroll)
{
#if WE_SCROLL_PANEL_USE_REBOUND
    if (scroll_y < -WE_SCROLL_PANEL_OVERSCROLL_LIMIT)
        scroll_y = -WE_SCROLL_PANEL_OVERSCROLL_LIMIT;
    if (scroll_y > max_scroll + WE_SCROLL_PANEL_OVERSCROLL_LIMIT)
        scroll_y = (int16_t)(max_scroll + WE_SCROLL_PANEL_OVERSCROLL_LIMIT);
#else
    if (scroll_y < 0)
        scroll_y = 0;
    if (scroll_y > max_scroll)
        scroll_y = max_scroll;
#endif
    return scroll_y;
}

static void _scroll_panel_apply_scroll_raw(we_scroll_panel_obj_t *obj, int16_t scroll_y)
{
    int16_t max_scroll;

    if (obj == NULL)
        return;

    max_scroll = _scroll_panel_max_scroll_y(obj);
    scroll_y = _scroll_panel_clamp_overscroll(scroll_y, max_scroll);
    if (obj->scroll_y == scroll_y)
        return;

    obj->scroll_y = scroll_y;
    obj->anim_scroll_y = scroll_y;
    we_scroll_panel_relayout(obj);
}

static void _scroll_panel_stop_anim(we_scroll_panel_obj_t *obj)
{
    if (obj == NULL)
        return;

    obj->animating = 0U;
    obj->drag_vy = 0;
    if (obj->task_id >= 0 && obj->base.lcd != NULL)
    {
        _we_gui_task_unregister(obj->base.lcd, obj->task_id);
        obj->task_id = -1;
    }
}

static void _scroll_panel_anim_task(we_lcd_t *lcd, void *user_data, uint16_t elapsed_ms)
{
    we_scroll_panel_obj_t *obj = (we_scroll_panel_obj_t *)user_data;
    int16_t max_scroll;
    int16_t overshoot;
    int16_t step;

    (void)lcd;

    if (obj == NULL || elapsed_ms == 0U)
        return;

    max_scroll = _scroll_panel_max_scroll_y(obj);

#if WE_SCROLL_PANEL_USE_INERTIA
    if (obj->drag_vy != 0)
    {
        step = (int16_t)((obj->drag_vy * (int16_t)elapsed_ms) / 16);
        if (step == 0)
            step = (obj->drag_vy > 0) ? 1 : -1;
        obj->anim_scroll_y = (int16_t)(obj->anim_scroll_y + step);
        obj->drag_vy = (int16_t)((obj->drag_vy * WE_SCROLL_PANEL_INERTIA_FRICTION_NUM) / WE_SCROLL_PANEL_INERTIA_FRICTION_DEN);
        if (obj->drag_vy == 0 && step != 0)
            obj->drag_vy = (step > 0) ? 1 : -1;
        if (obj->drag_vy == 1 || obj->drag_vy == -1)
        {
            if ((obj->anim_scroll_y >= 0 && obj->anim_scroll_y <= max_scroll) || elapsed_ms >= 16U)
                obj->drag_vy = 0;
        }
    }
#endif

#if WE_SCROLL_PANEL_USE_REBOUND
    if (obj->anim_scroll_y < 0)
    {
        overshoot = (int16_t)(-obj->anim_scroll_y);
        step = (int16_t)(overshoot / WE_SCROLL_PANEL_REBOUND_PULL_DIV);
        if (step < 1)
            step = 1;
        if (step > WE_SCROLL_PANEL_REBOUND_MAX_STEP)
            step = WE_SCROLL_PANEL_REBOUND_MAX_STEP;
        obj->anim_scroll_y = (obj->anim_scroll_y + step > 0) ? 0 : (int16_t)(obj->anim_scroll_y + step);
    }
    else if (obj->anim_scroll_y > max_scroll)
    {
        overshoot = (int16_t)(obj->anim_scroll_y - max_scroll);
        step = (int16_t)(overshoot / WE_SCROLL_PANEL_REBOUND_PULL_DIV);
        if (step < 1)
            step = 1;
        if (step > WE_SCROLL_PANEL_REBOUND_MAX_STEP)
            step = WE_SCROLL_PANEL_REBOUND_MAX_STEP;
        obj->anim_scroll_y = (obj->anim_scroll_y - step < max_scroll) ? max_scroll : (int16_t)(obj->anim_scroll_y - step);
    }
#else
    if (obj->anim_scroll_y < 0)
        obj->anim_scroll_y = 0;
    if (obj->anim_scroll_y > max_scroll)
        obj->anim_scroll_y = max_scroll;
#endif

    _scroll_panel_apply_scroll_raw(obj, obj->anim_scroll_y);

    if (obj->drag_vy == 0 && obj->anim_scroll_y >= 0 && obj->anim_scroll_y <= max_scroll)
        _scroll_panel_stop_anim(obj);
}

static void _scroll_panel_start_anim(we_scroll_panel_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;

    if (obj->task_id < 0)
        obj->task_id = _we_gui_task_register_with_data(obj->base.lcd, _scroll_panel_anim_task, obj);
    if (obj->task_id >= 0)
        obj->animating = 1U;
}

static we_scroll_panel_child_slot_t *_scroll_panel_find_slot(we_scroll_panel_obj_t *obj, we_obj_t *child)
{
    uint16_t i;

    if (obj == NULL || child == NULL)
        return NULL;

    for (i = 0U; i < WE_SCROLL_PANEL_CHILD_MAX; i++)
    {
        if (obj->child_slots[i].used && obj->child_slots[i].child == child)
            return &obj->child_slots[i];
    }

    return NULL;
}

static void _scroll_panel_detach_obj(we_obj_t *obj)
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

static int16_t _scroll_panel_max_scroll_y(const we_scroll_panel_obj_t *obj)
{
    int16_t viewport_h;
    int16_t max_scroll;

    if (obj == NULL)
        return 0;

    _scroll_panel_update_inner_rect((we_scroll_panel_obj_t *)obj);
    viewport_h = obj->inner_h;
    max_scroll = (int16_t)(obj->content_h - viewport_h);
    return (max_scroll > 0) ? max_scroll : 0;
}

static uint8_t _scroll_panel_need_scrollbar(const we_scroll_panel_obj_t *obj)
{
    if (obj == NULL)
        return 0U;

    _scroll_panel_update_inner_rect((we_scroll_panel_obj_t *)obj);
    if (!obj->show_scrollbar || obj->scrollbar_w == 0U)
        return 0U;
    if (obj->inner_h <= 0)
        return 0U;
    if (obj->content_h <= obj->inner_h)
        return 0U;

    return 1U;
}

static uint8_t _scroll_panel_get_scrollbar_thumb_rect(const we_scroll_panel_obj_t *obj,
                                                      int16_t *x, int16_t *y,
                                                      int16_t *w, int16_t *h)
{
    int16_t track_x;
    int16_t track_y;
    int16_t track_h;
    int16_t thumb_h;
    int16_t thumb_y;
    int16_t max_scroll;

    if (!_scroll_panel_need_scrollbar(obj))
        return 0U;

    track_x = (int16_t)(obj->base.x + obj->base.w - obj->scrollbar_w - 2);
    track_y = (int16_t)(obj->base.y + 4);
    track_h = (int16_t)(obj->base.h - 8);
    if (track_h <= 4)
        return 0U;

    thumb_h = (int16_t)(((int32_t)track_h * obj->inner_h) / obj->content_h);
    if (thumb_h < 8)
        thumb_h = 8;
    if (thumb_h > track_h)
        thumb_h = track_h;

    max_scroll = _scroll_panel_max_scroll_y(obj);
    if (max_scroll > 0)
        thumb_y = (int16_t)(track_y + ((int32_t)(track_h - thumb_h) * obj->scroll_y) / max_scroll);
    else
        thumb_y = track_y;

    if (x != NULL) *x = track_x;
    if (y != NULL) *y = thumb_y;
    if (w != NULL) *w = obj->scrollbar_w;
    if (h != NULL) *h = thumb_h;
    return 1U;
}

static void _scroll_panel_update_child_abs(we_scroll_panel_obj_t *obj, we_scroll_panel_child_slot_t *slot)
{
    if (obj == NULL || slot == NULL || !slot->used || slot->child == NULL)
        return;

    we_obj_set_pos(slot->child,
                   (int16_t)(obj->base.x + slot->local_x),
                   (int16_t)(obj->base.y + slot->local_y - obj->scroll_y));
}

static void _scroll_panel_draw_scrollbar(we_scroll_panel_obj_t *obj)
{
    we_lcd_t *lcd;
    int16_t thumb_x;
    int16_t thumb_y;
    int16_t thumb_w;
    int16_t thumb_h;

    if (obj == NULL)
        return;

    lcd = (we_lcd_t *)obj->base.lcd;
    if (lcd == NULL || obj->scrollbar_opacity == 0U)
        return;

    if (!_scroll_panel_get_scrollbar_thumb_rect(obj, &thumb_x, &thumb_y, &thumb_w, &thumb_h))
        return;

    we_draw_round_rect_analytic_fill(lcd,
                                     thumb_x, thumb_y,
                                     (uint16_t)thumb_w, (uint16_t)thumb_h,
                                     (uint16_t)(thumb_w / 2U),
                                     obj->scrollbar_color, obj->scrollbar_opacity);
}

static void _scroll_panel_draw_core(we_scroll_panel_obj_t *obj, uint8_t draw_children, uint8_t draw_scrollbar)
{
    we_lcd_t *lcd;
    we_obj_t *child;
    we_area_t old_pfb_area;
    uint16_t old_y_start;
    uint16_t old_y_end;
    colour_t *old_gram;
    int16_t new_x0;
    int16_t new_y0;
    int16_t new_x1;
    int16_t new_y1;

    if (obj == NULL || obj->opacity == 0U)
        return;

    lcd = (we_lcd_t *)obj->base.lcd;
    if (lcd == NULL)
        return;

    _scroll_panel_update_inner_rect(obj);

    we_fill_rect(lcd, obj->base.x, obj->base.y,
                 (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                 obj->border_color, obj->opacity);

    if (obj->inner_w > 0 && obj->inner_h > 0)
    {
        we_fill_rect(lcd, obj->inner_x, obj->inner_y,
                     (uint16_t)obj->inner_w, (uint16_t)obj->inner_h,
                     obj->bg_color, obj->opacity);
    }

    old_pfb_area = lcd->pfb_area;
    old_y_start = lcd->pfb_y_start;
    old_y_end = lcd->pfb_y_end;
    old_gram = lcd->pfb_gram;

    new_x0 = WE_MAX(old_pfb_area.x0, obj->inner_x);
    new_y0 = WE_MAX(old_y_start, obj->inner_y);
    new_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(obj->inner_x + obj->inner_w - 1));
    new_y1 = WE_MIN(old_y_end, (int16_t)(obj->inner_y + obj->inner_h - 1));

    if (new_x0 <= new_x1 && new_y0 <= new_y1 && draw_children)
    {
        lcd->pfb_area.x0 = new_x0;
        lcd->pfb_area.x1 = new_x1;
        lcd->pfb_y_start = new_y0;
        lcd->pfb_y_end = new_y1;
        lcd->pfb_gram = old_gram + (new_y0 - old_y_start) * lcd->pfb_width + (new_x0 - old_pfb_area.x0);

        child = obj->children_head;
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

    if (draw_scrollbar)
        _scroll_panel_draw_scrollbar(obj);
}

static we_obj_t *_scroll_panel_hit_child(we_scroll_panel_obj_t *obj, int16_t x, int16_t y)
{
    we_obj_t *child;
    we_obj_t *target;

    if (obj == NULL)
        return NULL;

    target = NULL;
    child = obj->children_head;
    while (child != NULL)
    {
        if (child->class_p != NULL && child->class_p->event_cb != NULL)
        {
            if (x >= child->x && x < (child->x + child->w) && y >= child->y && y < (child->y + child->h))
            {
                target = child;
            }
        }
        child = child->next;
    }

    return target;
}

static uint8_t _scroll_panel_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_scroll_panel_obj_t *obj = (we_scroll_panel_obj_t *)ptr;
    int16_t dy_total;
    int16_t dy_step;
    uint8_t inside;

    if (obj == NULL || data == NULL)
        return 0U;

    _scroll_panel_update_inner_rect(obj);
    inside = (uint8_t)(data->x >= obj->inner_x && data->x < (obj->inner_x + obj->inner_w) &&
                       data->y >= obj->inner_y && data->y < (obj->inner_y + obj->inner_h));

    if (event == WE_EVENT_PRESSED)
    {
        we_obj_t *child;

        if (!inside)
            return 0U;

        _scroll_panel_stop_anim(obj);
        obj->anim_scroll_y = obj->scroll_y;
        obj->drag_vy = 0;

        child = _scroll_panel_hit_child(obj, data->x, data->y);
        obj->tracking_press = 1U;
        obj->dragging = 0U;
        obj->press_start_y = data->y;
        obj->last_touch_y = data->y;
        obj->last_pressed_child = child;

        if (child != NULL)
            child->class_p->event_cb(child, WE_EVENT_PRESSED, data);
        return 1U;
    }

    if (!obj->tracking_press)
        return inside ? 1U : 0U;

    if (event == WE_EVENT_STAY)
    {
        dy_total = (int16_t)(data->y - obj->press_start_y);
        if (!obj->dragging)
        {
            int16_t abs_dy = (dy_total >= 0) ? dy_total : (int16_t)(-dy_total);
            if (abs_dy >= WE_SCROLL_PANEL_DRAG_THRESHOLD)
            {
                obj->dragging = 1U;
                if (obj->last_pressed_child != NULL)
                {
                    obj->last_pressed_child->class_p->event_cb(obj->last_pressed_child, WE_EVENT_RELEASED, data);
                    obj->last_pressed_child = NULL;
                }
            }
        }

        if (obj->dragging)
        {
            dy_step = (int16_t)(data->y - obj->last_touch_y);
            if (dy_step != 0)
            {
                obj->drag_vy = (int16_t)((-dy_step * WE_SCROLL_PANEL_VELOCITY_GAIN_NUM) / WE_SCROLL_PANEL_VELOCITY_GAIN_DEN);
                _scroll_panel_apply_scroll_raw(obj, (int16_t)(obj->scroll_y - dy_step));
                obj->last_touch_y = data->y;
            }
        }
        else if (obj->last_pressed_child != NULL)
        {
            obj->last_pressed_child->class_p->event_cb(obj->last_pressed_child, WE_EVENT_STAY, data);
        }
        return 1U;
    }

    if (event == WE_EVENT_RELEASED)
    {
        int16_t max_scroll = _scroll_panel_max_scroll_y(obj);

        if (!obj->dragging && obj->last_pressed_child != NULL)
        {
            obj->last_pressed_child->class_p->event_cb(obj->last_pressed_child, WE_EVENT_RELEASED, data);
        }
        else
        {
#if WE_SCROLL_PANEL_USE_INERTIA
            if (obj->drag_vy != 0)
                _scroll_panel_start_anim(obj);
            else
#endif
#if WE_SCROLL_PANEL_USE_REBOUND
            if (obj->scroll_y < 0 || obj->scroll_y > max_scroll)
                _scroll_panel_start_anim(obj);
            else
#endif
            {
                obj->scroll_y = _scroll_panel_clamp_to_bounds(obj, obj->scroll_y);
                obj->anim_scroll_y = obj->scroll_y;
                _scroll_panel_stop_anim(obj);
                we_scroll_panel_relayout(obj);
            }
        }
        obj->tracking_press = 0U;
        obj->dragging = 0U;
        return 1U;
    }

    if (event == WE_EVENT_CLICKED)
    {
        if (!obj->dragging && obj->last_pressed_child != NULL)
        {
            if (data->x >= obj->last_pressed_child->x && data->x < (obj->last_pressed_child->x + obj->last_pressed_child->w) &&
                data->y >= obj->last_pressed_child->y && data->y < (obj->last_pressed_child->y + obj->last_pressed_child->h))
            {
                obj->last_pressed_child->class_p->event_cb(obj->last_pressed_child, WE_EVENT_CLICKED, data);
            }
        }
        obj->last_pressed_child = NULL;
        obj->tracking_press = 0U;
        obj->dragging = 0U;
        return 1U;
    }

    if (event == WE_EVENT_SWIPE_UP)
    {
        _scroll_panel_stop_anim(obj);
        we_scroll_panel_set_scroll_y(obj, (int16_t)(obj->scroll_y + WE_CFG_SWIPE_THRESHOLD));
        obj->last_pressed_child = NULL;
        obj->tracking_press = 0U;
        obj->dragging = 0U;
        return 1U;
    }

    if (event == WE_EVENT_SWIPE_DOWN)
    {
        _scroll_panel_stop_anim(obj);
        we_scroll_panel_set_scroll_y(obj, (int16_t)(obj->scroll_y - WE_CFG_SWIPE_THRESHOLD));
        obj->last_pressed_child = NULL;
        obj->tracking_press = 0U;
        obj->dragging = 0U;
        return 1U;
    }

    return 1U;
}

static void _scroll_panel_draw_cb(void *ptr)
{
    _scroll_panel_draw_core((we_scroll_panel_obj_t *)ptr, 1U, 1U);
}

void we_scroll_panel_obj_init(we_scroll_panel_obj_t *obj, we_lcd_t *lcd,
                              int16_t x, int16_t y, int16_t w, int16_t h,
                              colour_t bg_color, colour_t border_color,
                              uint16_t radius, uint8_t opacity)
{
    static const we_class_t _scroll_panel_class = { .draw_cb = _scroll_panel_draw_cb, .event_cb = _scroll_panel_event_cb };
    uint16_t i;

    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_scroll_panel_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;
    obj->task_id = -1;
    obj->children_head = NULL;
    obj->scroll_y = 0;
    obj->content_h = h;
    obj->radius = radius;
    obj->opacity = opacity;
    obj->show_scrollbar = 0U;
    obj->scrollbar_w = WE_SCROLL_PANEL_SCROLLBAR_W;
    obj->scrollbar_opacity = 140U;
    obj->bg_color = bg_color;
    obj->border_color = border_color;
    obj->scrollbar_color = border_color;
    obj->last_pressed_child = NULL;
    obj->press_start_y = 0;
    obj->last_touch_y = 0;
    obj->drag_vy = 0;
    obj->anim_scroll_y = 0;
    obj->tracking_press = 0U;
    obj->dragging = 0U;
    obj->animating = 0U;
    _scroll_panel_update_inner_rect(obj);

    for (i = 0U; i < WE_SCROLL_PANEL_CHILD_MAX; i++)
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

void we_scroll_panel_obj_delete(we_scroll_panel_obj_t *obj)
{
    we_obj_t *child;
    we_obj_t *next;
    uint16_t i;

    if (obj == NULL || obj->base.lcd == NULL)
        return;

    _scroll_panel_stop_anim(obj);

    child = obj->children_head;
    while (child != NULL)
    {
        next = child->next;
        we_obj_delete(child);
        child = next;
    }

    obj->children_head = NULL;
    for (i = 0U; i < WE_SCROLL_PANEL_CHILD_MAX; i++)
        obj->child_slots[i].used = 0U;

    we_obj_delete((we_obj_t *)obj);
}

void we_scroll_panel_add_child(we_scroll_panel_obj_t *obj, we_obj_t *child)
{
    uint16_t i;

    if (obj == NULL || child == NULL)
        return;
    if (child == (we_obj_t *)obj)
        return;
    if (child->lcd != obj->base.lcd)
        return;
    if (_scroll_panel_find_slot(obj, child) != NULL)
        return;

    for (i = 0U; i < WE_SCROLL_PANEL_CHILD_MAX; i++)
    {
        if (!obj->child_slots[i].used)
        {
            _scroll_panel_detach_obj(child);
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
            _scroll_panel_update_child_abs(obj, &obj->child_slots[i]);
            return;
        }
    }
}

void we_scroll_panel_remove_child(we_scroll_panel_obj_t *obj, we_obj_t *child)
{
    we_scroll_panel_child_slot_t *slot;

    if (obj == NULL || child == NULL)
        return;

    slot = _scroll_panel_find_slot(obj, child);
    if (slot == NULL)
        return;

    _scroll_panel_detach_obj(child);
    slot->used = 0U;
    slot->child = NULL;
    slot->local_x = 0;
    slot->local_y = 0;
}

void we_scroll_panel_set_child_pos(we_scroll_panel_obj_t *obj, we_obj_t *child, int16_t local_x, int16_t local_y)
{
    we_scroll_panel_child_slot_t *slot;

    if (obj == NULL || child == NULL)
        return;

    slot = _scroll_panel_find_slot(obj, child);
    if (slot == NULL)
        return;

    slot->local_x = local_x;
    slot->local_y = local_y;
    _scroll_panel_update_child_abs(obj, slot);
}

void we_scroll_panel_relayout(we_scroll_panel_obj_t *obj)
{
    uint16_t i;

    if (obj == NULL)
        return;

    for (i = 0U; i < WE_SCROLL_PANEL_CHILD_MAX; i++)
    {
        if (obj->child_slots[i].used)
            _scroll_panel_update_child_abs(obj, &obj->child_slots[i]);
    }

    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_set_scroll_y(we_scroll_panel_obj_t *obj, int16_t scroll_y)
{
    int16_t max_scroll;

    if (obj == NULL)
        return;

    _scroll_panel_stop_anim(obj);
    max_scroll = _scroll_panel_max_scroll_y(obj);
    if (scroll_y < 0)
        scroll_y = 0;
    if (scroll_y > max_scroll)
        scroll_y = max_scroll;
    if (obj->scroll_y == scroll_y)
        return;

    obj->scroll_y = scroll_y;
    obj->anim_scroll_y = scroll_y;
    obj->drag_vy = 0;
    we_scroll_panel_relayout(obj);
}

void we_scroll_panel_set_content_h(we_scroll_panel_obj_t *obj, int16_t content_h)
{
    if (obj == NULL)
        return;
    _scroll_panel_stop_anim(obj);
    obj->content_h = (content_h > obj->base.h) ? content_h : obj->base.h;
    if (obj->scroll_y > _scroll_panel_max_scroll_y(obj))
        obj->scroll_y = _scroll_panel_max_scroll_y(obj);
    obj->anim_scroll_y = obj->scroll_y;
    obj->drag_vy = 0;
    we_scroll_panel_relayout(obj);
}

void we_scroll_panel_set_radius(we_scroll_panel_obj_t *obj, uint16_t radius)
{
    if (obj == NULL)
        return;
    obj->radius = radius;
    _scroll_panel_update_inner_rect(obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_set_colors(we_scroll_panel_obj_t *obj,
                                colour_t bg_color,
                                colour_t border_color,
                                colour_t scrollbar_color)
{
    if (obj == NULL)
        return;
    obj->bg_color = bg_color;
    obj->border_color = border_color;
    obj->scrollbar_color = scrollbar_color;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_set_scrollbar(we_scroll_panel_obj_t *obj, uint8_t show_scrollbar, uint8_t scrollbar_w)
{
    if (obj == NULL)
        return;
    obj->show_scrollbar = show_scrollbar ? 1U : 0U;
    obj->scrollbar_w = (scrollbar_w > 0U) ? scrollbar_w : WE_SCROLL_PANEL_SCROLLBAR_W;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_set_scrollbar_opacity(we_scroll_panel_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL)
        return;
    obj->scrollbar_opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_set_opacity(we_scroll_panel_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_draw_only(we_scroll_panel_obj_t *obj)
{
    _scroll_panel_draw_core(obj, 0U, 0U);
}

void we_scroll_panel_draw_scrollbar_overlay(we_scroll_panel_obj_t *obj)
{
    if (obj == NULL)
        return;
    _scroll_panel_update_inner_rect(obj);
    _scroll_panel_draw_scrollbar(obj);
}

void we_scroll_panel_get_inner_rect(const we_scroll_panel_obj_t *obj,
                                    int16_t *x, int16_t *y,
                                    int16_t *w, int16_t *h)
{
    if (obj == NULL)
        return;
    _scroll_panel_update_inner_rect((we_scroll_panel_obj_t *)obj);
    if (x != NULL) *x = obj->inner_x;
    if (y != NULL) *y = obj->inner_y;
    if (w != NULL) *w = obj->inner_w;
    if (h != NULL) *h = obj->inner_h;
}
