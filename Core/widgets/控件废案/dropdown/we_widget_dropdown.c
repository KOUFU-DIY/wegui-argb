#include "we_widget_dropdown.h"
#include "we_render.h"

static void _dropdown_draw_cb(void *ptr);
static uint8_t _dropdown_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);
static void _dropdown_popup_draw_cb(void *ptr);
static uint8_t _dropdown_popup_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);

static const we_class_t _dropdown_class = {
    .draw_cb = _dropdown_draw_cb,
    .event_cb = _dropdown_event_cb
};

static const we_class_t _dropdown_popup_class = {
    .draw_cb = _dropdown_popup_draw_cb,
    .event_cb = _dropdown_popup_event_cb
};

static uint8_t _dropdown_clamp_index(const we_dropdown_obj_t *obj, uint8_t index)
{
    if (obj == NULL || obj->item_count == 0U)
        return 0U;
    if (index >= obj->item_count)
        return (uint8_t)(obj->item_count - 1U);
    return index;
}

static uint8_t _dropdown_visible_rows(const we_dropdown_obj_t *obj)
{
    uint8_t rows;

    if (obj == NULL)
        return 0U;

    rows = obj->item_count;
    if (rows > obj->visible_rows)
        rows = obj->visible_rows;
    return rows;
}

static int16_t _dropdown_panel_height(const we_dropdown_obj_t *obj)
{
    if (obj == NULL || obj->item_count == 0U)
        return 0;
    return (int16_t)(_dropdown_visible_rows(obj) * obj->row_height + 2);
}

static int16_t _dropdown_panel_view_height(const we_dropdown_obj_t *obj)
{
    if (obj == NULL)
        return 0;

    if (obj->popup.panel.inner_h > 0)
        return obj->popup.panel.inner_h;

    {
        int16_t panel_h = _dropdown_panel_height(obj);
        return (panel_h > 2) ? (int16_t)(panel_h - 2) : panel_h;
    }
}

static void _dropdown_sync_popup_scroll(we_dropdown_obj_t *obj)
{
    int16_t max_scroll;
    int16_t selected_top;
    int16_t selected_bottom;
    int16_t view_h;

    if (obj == NULL || obj->item_count == 0U)
        return;

    view_h = _dropdown_panel_view_height(obj);
    max_scroll = (int16_t)(obj->item_count * obj->row_height - view_h);
    selected_top = (int16_t)(obj->selected_index * obj->row_height);
    selected_bottom = (int16_t)(selected_top + obj->row_height);

    if (max_scroll < 0)
        max_scroll = 0;

    if (selected_bottom > obj->popup.panel.scroll_y + view_h)
        obj->popup.panel.scroll_y = (int16_t)(selected_bottom - view_h);
    if (selected_top < obj->popup.panel.scroll_y)
        obj->popup.panel.scroll_y = selected_top;
    if (obj->popup.panel.scroll_y > max_scroll)
        obj->popup.panel.scroll_y = max_scroll;
    if (obj->popup.panel.scroll_y < 0)
        obj->popup.panel.scroll_y = 0;
}

static int16_t _dropdown_panel_y(const we_dropdown_obj_t *obj)
{
    return (int16_t)(obj->base.y + obj->base.h);
}

static void _dropdown_invalidate_main(we_dropdown_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_invalidate((we_obj_t *)obj);
}

static void _dropdown_invalidate_panel(we_dropdown_obj_t *obj)
{
    int16_t panel_h;

    if (obj == NULL)
        return;

    panel_h = _dropdown_panel_height(obj);
    if (panel_h <= 0)
        return;

    we_obj_invalidate_area((we_obj_t *)&obj->popup,
                           obj->base.x,
                           _dropdown_panel_y(obj),
                           obj->base.w,
                           panel_h);
}

static void _dropdown_invalidate_all(we_dropdown_obj_t *obj)
{
    _dropdown_invalidate_main(obj);
    _dropdown_invalidate_panel(obj);
}

static void _dropdown_draw_arrow(we_lcd_t *lcd, int16_t cx, int16_t cy, uint8_t size,
                                 colour_t color, uint8_t opacity)
{
    uint8_t row;

    if (lcd == NULL)
        return;

    for (row = 0U; row <= size; row++)
    {
        int16_t line_half_w = (int16_t)row;
        int16_t lx = (int16_t)(cx - line_half_w);
        int16_t ly = (int16_t)(cy - (int16_t)size / 2 + row);
        int16_t rw = (int16_t)(line_half_w * 2 + 1);
        we_draw_round_rect_analytic_fill(lcd, lx, ly, (uint16_t)rw, 1U, 0U, color, opacity);
    }
}

static colour_t _dropdown_item_bg_color(const we_dropdown_obj_t *obj, uint8_t index)
{
    if (obj == NULL)
        return RGB888TODEV(0, 0, 0);

    if (obj->pressed_part == (uint8_t)(2U + index))
        return obj->pressed_color;
    if (obj->selected_index == index)
        return obj->selected_color;
    return obj->panel_color;
}

static void _dropdown_draw_item_highlight(we_lcd_t *lcd, const we_dropdown_popup_obj_t *popup,
                                          int16_t inner_x, int16_t inner_y,
                                          int16_t inner_w, int16_t inner_h,
                                          int16_t row_y, int16_t row_bottom,
                                          colour_t bg, uint8_t opacity)
{
    int16_t fill_y;
    int16_t fill_h;

    (void)popup;

    if (lcd == NULL || opacity == 0U || inner_w <= 0 || inner_h <= 0)
        return;

    fill_y = WE_MAX(row_y, inner_y);
    fill_h = WE_MIN(row_bottom, (int16_t)(inner_y + inner_h)) - fill_y;
    if (fill_h <= 0)
        return;

    we_fill_rect(lcd,
                 inner_x, fill_y,
                 (uint16_t)inner_w, (uint16_t)fill_h,
                 bg, opacity);
}


static void _dropdown_draw_text(we_lcd_t *lcd, const unsigned char *font,
                                const char *text, int16_t x, int16_t y, int16_t w, int16_t h,
                                colour_t color, uint8_t opacity)
{
    uint16_t text_w;
    uint16_t line_h;
    int16_t tx;
    int16_t ty;

    if (lcd == NULL || font == NULL || text == NULL)
        return;

    text_w = we_get_text_width(font, text);
    line_h = we_font_get_line_height(font);
    tx = (int16_t)(x + 8);
    ty = (int16_t)(y + (h - (int16_t)line_h) / 2);

    if (text_w + 8U > (uint16_t)w && w > 8)
        tx = (int16_t)(x + 4);

    we_draw_string(lcd, tx, ty, font, text, color, opacity);
}

static uint8_t _dropdown_hit_panel_item(const we_dropdown_obj_t *obj, int16_t px, int16_t py)
{
    int16_t inner_x;
    int16_t inner_y;
    int16_t inner_w;
    int16_t inner_h;
    uint8_t idx;
    int16_t rel_y;

    if (obj == NULL || obj->expanded == 0U)
        return 0U;

    we_scroll_panel_get_inner_rect((we_scroll_panel_obj_t *)&obj->popup.panel,
                                   &inner_x, &inner_y, &inner_w, &inner_h);
    if (inner_w <= 0 || inner_h <= 0)
        return 0U;

    if (!(px >= inner_x && px < inner_x + inner_w && py >= inner_y && py < inner_y + inner_h))
        return 0U;

    rel_y = (int16_t)(py - inner_y + obj->popup.panel.scroll_y);
    if (rel_y < 0)
        return 0U;

    idx = (uint8_t)(rel_y / obj->row_height);
    if (idx >= obj->item_count)
        return 0U;

    return (uint8_t)(2U + idx);
}

static void _dropdown_popup_attach(we_dropdown_obj_t *obj)
{
    we_lcd_t *lcd;
    we_obj_t *tail;
    int16_t panel_h;

    if (obj == NULL)
        return;

    lcd = (we_lcd_t *)obj->base.lcd;
    if (lcd == NULL)
        return;

    panel_h = _dropdown_panel_height(obj);
    if (panel_h <= 0)
        return;

    obj->popup.owner = obj;
    obj->popup.base.class_p = &_dropdown_popup_class;
    obj->popup.base.parent = NULL;
    obj->popup.base.lcd = (struct we_lcd_t *)lcd;
    obj->popup.base.x = 0;
    obj->popup.base.y = 0;
    obj->popup.base.w = lcd->width;
    obj->popup.base.h = lcd->height;

    obj->popup.panel.base.lcd = lcd;
    obj->popup.panel.base.x = obj->base.x;
    obj->popup.panel.base.y = _dropdown_panel_y(obj);
    obj->popup.panel.base.w = obj->base.w;
    obj->popup.panel.base.h = panel_h;
    obj->popup.panel.content_h = (int16_t)(obj->item_count * obj->row_height);
    obj->popup.panel.scroll_y = 0;
    _dropdown_sync_popup_scroll(obj);
    obj->popup.panel.radius = WE_DROPDOWN_RADIUS;
    obj->popup.panel.opacity = obj->opacity;
    obj->popup.panel.show_scrollbar = (obj->show_scrollbar && obj->item_count > obj->visible_rows) ? 1U : 0U;
    obj->popup.panel.scrollbar_w = 4U;
    obj->popup.panel.bg_color = obj->panel_color;
    obj->popup.panel.border_color = obj->border_color;
    obj->popup.panel.scrollbar_color = obj->hint_color;
    we_scroll_panel_get_inner_rect(&obj->popup.panel, NULL, NULL, NULL, NULL);

    tail = lcd->obj_list_head;
    if (tail == NULL)
    {
        lcd->obj_list_head = (we_obj_t *)&obj->popup;
    }
    else
    {
        while (tail->next != NULL)
            tail = tail->next;
        tail->next = (we_obj_t *)&obj->popup;
    }
    obj->popup.base.next = NULL;
    we_obj_bring_to_front((we_obj_t *)&obj->popup);
}

static void _dropdown_popup_detach(we_dropdown_obj_t *obj)
{
    we_obj_t *curr;
    we_obj_t *prev;
    we_lcd_t *lcd;

    if (obj == NULL || obj->popup.base.lcd == NULL)
        return;

    lcd = (we_lcd_t *)obj->base.lcd;
    curr = lcd->obj_list_head;
    prev = NULL;
    while (curr != NULL)
    {
        if (curr == (we_obj_t *)&obj->popup)
        {
            if (prev == NULL)
                lcd->obj_list_head = curr->next;
            else
                prev->next = curr->next;
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    obj->popup.base.next = NULL;
    obj->popup.base.parent = NULL;
    obj->popup.base.lcd = NULL;
    obj->popup.base.class_p = NULL;
}

static void _dropdown_set_expanded_internal(we_dropdown_obj_t *obj, uint8_t expanded)
{
    if (obj == NULL)
        return;

    expanded = expanded ? 1U : 0U;
    if (obj->expanded == expanded)
        return;

    if (expanded)
    {
        obj->expanded = 1U;
        _dropdown_popup_attach(obj);
    }
    else
    {
        _dropdown_invalidate_panel(obj);
        _dropdown_popup_detach(obj);
        obj->expanded = 0U;
    }

    obj->pressed_part = 0U;
    obj->armed_part = 0U;
}

static void _dropdown_draw_cb(void *ptr)
{
    we_dropdown_obj_t *obj = (we_dropdown_obj_t *)ptr;
    we_lcd_t *lcd;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    int16_t inner_x;
    int16_t inner_y;
    int16_t inner_w;
    int16_t inner_h;
    int16_t arrow_x;
    int16_t arrow_y;
    int16_t arrow_box_x;
    uint8_t arrow_size;
    const char *text;
    colour_t box_fill;
    colour_t arrow_fill;
    uint16_t inner_r;

    if (obj == NULL || obj->opacity == 0U)
        return;

    lcd = (we_lcd_t *)obj->base.lcd;
    if (lcd == NULL)
        return;

    x = obj->base.x;
    y = obj->base.y;
    w = obj->base.w;
    h = obj->base.h;
    if (w <= 2 || h <= 2)
        return;

    inner_x = (int16_t)(x + 1);
    inner_y = (int16_t)(y + 1);
    inner_w = (int16_t)(w - 2);
    inner_h = (int16_t)(h - 2);
    inner_r = (uint16_t)(WE_DROPDOWN_RADIUS > 1U ? WE_DROPDOWN_RADIUS - 1U : 0U);

    box_fill = (obj->pressed_part == 1U) ? obj->pressed_color : obj->box_color;
    arrow_fill = (obj->pressed_part == 1U) ? obj->pressed_color : obj->panel_color;

    we_draw_round_rect_analytic_fill(lcd, x, y, (uint16_t)w, (uint16_t)h,
                                     (uint16_t)WE_DROPDOWN_RADIUS,
                                     obj->border_color,
                                     obj->opacity);

    we_draw_round_rect_analytic_fill(lcd, inner_x, inner_y, (uint16_t)inner_w, (uint16_t)inner_h,
                                     inner_r,
                                     box_fill,
                                     obj->opacity);

    arrow_box_x = (int16_t)(inner_x + inner_w - WE_DROPDOWN_ARROW_W);
    we_draw_round_rect_analytic_fill(lcd,
                                     arrow_box_x, inner_y,
                                     (uint16_t)WE_DROPDOWN_ARROW_W, (uint16_t)inner_h,
                                     inner_r,
                                     arrow_fill,
                                     obj->opacity);

    we_draw_line(lcd,
                 (int16_t)(arrow_box_x - 1), (int16_t)(inner_y + 5),
                 (int16_t)(arrow_box_x - 1), (int16_t)(inner_y + inner_h - 6),
                 1U, obj->border_color, obj->opacity);

    text = we_dropdown_get_selected_text(obj);
    if (text == NULL)
        text = "";
    _dropdown_draw_text(lcd, obj->font, text, inner_x, inner_y,
                        (int16_t)(inner_w - WE_DROPDOWN_ARROW_W - 10), inner_h,
                        obj->text_color, obj->opacity);

    arrow_x = (int16_t)(arrow_box_x + WE_DROPDOWN_ARROW_W / 2 - 1);
    arrow_y = (int16_t)(y + h / 2 - 2);
    arrow_size = 4U;
    _dropdown_draw_arrow(lcd, arrow_x, arrow_y, arrow_size, obj->hint_color, obj->opacity);
}

static void _dropdown_popup_draw_cb(void *ptr)
{
    we_dropdown_popup_obj_t *popup = (we_dropdown_popup_obj_t *)ptr;
    we_dropdown_obj_t *obj;
    we_lcd_t *lcd;
    we_area_t old_pfb_area;
    uint16_t old_y_start;
    uint16_t old_y_end;
    colour_t *old_gram;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t panel_h;
    int16_t inner_x;
    int16_t inner_y;
    int16_t inner_w;
    int16_t inner_h;
    int16_t right_safe_x;
    int16_t text_w;
    int16_t line_right_x;
    int16_t new_x0;
    int16_t new_y0;
    int16_t new_x1;
    int16_t new_y1;
    uint8_t i;

    if (popup == NULL || popup->owner == NULL)
        return;

    obj = popup->owner;
    if (obj->expanded == 0U || obj->opacity == 0U)
        return;

    lcd = (we_lcd_t *)obj->base.lcd;
    if (lcd == NULL)
        return;

    x = obj->base.x;
    y = _dropdown_panel_y(obj);
    w = obj->base.w;
    panel_h = _dropdown_panel_height(obj);
    if (panel_h <= 2 || w <= 2)
        return;

    popup->panel.base.x = x;
    popup->panel.base.y = y;
    popup->panel.base.w = w;
    popup->panel.base.h = panel_h;
    popup->panel.content_h = (int16_t)(obj->item_count * obj->row_height);
    popup->panel.opacity = obj->opacity;
    popup->panel.bg_color = obj->panel_color;
    popup->panel.border_color = obj->border_color;
    popup->panel.scrollbar_color = obj->hint_color;
    popup->panel.show_scrollbar = (obj->show_scrollbar && obj->item_count > obj->visible_rows) ? 1U : 0U;
    popup->panel.scrollbar_opacity = 140U;
    popup->panel.radius = WE_DROPDOWN_RADIUS;

    we_scroll_panel_draw_only(&popup->panel);
    we_scroll_panel_get_inner_rect(&popup->panel, &inner_x, &inner_y, &inner_w, &inner_h);
    if (inner_w <= 0 || inner_h <= 0)
        return;

    right_safe_x = (int16_t)(inner_x + inner_w);
    if (popup->panel.show_scrollbar)
        right_safe_x = (int16_t)(right_safe_x - popup->panel.scrollbar_w - 10);

    text_w = (int16_t)(right_safe_x - inner_x - 6);
    line_right_x = (int16_t)(right_safe_x - 2);

    old_pfb_area = lcd->pfb_area;
    old_y_start = lcd->pfb_y_start;
    old_y_end = lcd->pfb_y_end;
    old_gram = lcd->pfb_gram;

    new_x0 = WE_MAX(old_pfb_area.x0, inner_x);
    new_y0 = WE_MAX(old_y_start, inner_y);
    new_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(inner_x + inner_w - 1));
    new_y1 = WE_MIN(old_y_end, (int16_t)(inner_y + inner_h - 1));
    if (new_x0 > new_x1 || new_y0 > new_y1)
        return;

    lcd->pfb_area.x0 = new_x0;
    lcd->pfb_area.x1 = new_x1;
    lcd->pfb_y_start = new_y0;
    lcd->pfb_y_end = new_y1;
    lcd->pfb_gram = old_gram + (new_y0 - old_y_start) * lcd->pfb_width + (new_x0 - old_pfb_area.x0);

    for (i = 0U; i < obj->item_count; i++)
    {
        int16_t row_y = (int16_t)(inner_y + i * obj->row_height - popup->panel.scroll_y);
        int16_t row_bottom = (int16_t)(row_y + obj->row_height);
        int16_t fill_y;
        int16_t fill_h;
        colour_t bg = obj->panel_color;
        const char *item_text = obj->items[i];

        if (row_bottom <= inner_y || row_y >= inner_y + inner_h)
            continue;

        if (item_text == NULL)
            item_text = "";

        bg = _dropdown_item_bg_color(obj, i);
        if ((obj->pressed_part == (uint8_t)(2U + i) || obj->selected_index == i) && inner_w > 0)
        {
            _dropdown_draw_item_highlight(lcd, popup,
                                          inner_x, inner_y,
                                          inner_w, inner_h,
                                          row_y, row_bottom,
                                          bg, obj->opacity);
        }

        if (text_w > 2)
        {
            _dropdown_draw_text(lcd, obj->font, item_text,
                                (int16_t)(inner_x + 1), row_y,
                                (int16_t)(text_w - 2),
                                obj->row_height,
                                obj->text_color, obj->opacity);
        }

        if (i + 1U < obj->item_count)
        {
            int16_t line_y = (int16_t)(row_y + obj->row_height - 1);
            if (line_y >= inner_y && line_y < inner_y + inner_h && line_right_x > inner_x + 8)
            {
                we_draw_line(lcd,
                             (int16_t)(inner_x + 8), line_y,
                             line_right_x,
                             line_y,
                             1U, obj->border_color, obj->opacity);
            }
        }
    }

    lcd->pfb_area = old_pfb_area;
    lcd->pfb_y_start = old_y_start;
    lcd->pfb_y_end = old_y_end;
    lcd->pfb_gram = old_gram;

    we_scroll_panel_draw_scrollbar_overlay(&popup->panel);
}

static uint8_t _dropdown_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_dropdown_obj_t *obj = (we_dropdown_obj_t *)ptr;
    uint8_t old_pressed;
    uint8_t old_expanded;

    if (obj == NULL || data == NULL)
        return 0U;

    old_pressed = obj->pressed_part;
    old_expanded = obj->expanded;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        obj->pressed_part = 1U;
        obj->armed_part = 1U;
        break;

    case WE_EVENT_RELEASED:
        obj->pressed_part = 0U;
        break;

    case WE_EVENT_CLICKED:
        if (obj->armed_part == 1U)
        {
            _dropdown_set_expanded_internal(obj, (uint8_t)!obj->expanded);
            if (obj->expanded)
            {
                _dropdown_sync_popup_scroll(obj);
                we_obj_bring_to_front((we_obj_t *)&obj->popup);
            }
        }
        obj->pressed_part = 0U;
        obj->armed_part = 0U;
        break;

    default:
        break;
    }

    if (old_expanded != obj->expanded)
        _dropdown_invalidate_all(obj);
    else if (old_pressed != obj->pressed_part)
        _dropdown_invalidate_main(obj);

    return 1U;
}

static uint8_t _dropdown_popup_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_dropdown_popup_obj_t *popup = (we_dropdown_popup_obj_t *)ptr;
    we_dropdown_obj_t *obj;
    uint8_t hit;
    uint8_t old_pressed;
    uint8_t old_selected;
    uint8_t old_expanded;

    if (popup == NULL || popup->owner == NULL || data == NULL)
        return 0U;

    obj = popup->owner;
    old_pressed = obj->pressed_part;
    old_selected = obj->selected_index;
    old_expanded = obj->expanded;
    hit = _dropdown_hit_panel_item(obj, data->x, data->y);

    switch (event)
    {
    case WE_EVENT_PRESSED:
        obj->dragging = 1U;
        obj->drag_moved = 0U;
        obj->drag_start_y = data->y;
        obj->drag_start_scroll_y = popup->panel.scroll_y;
        if (hit != 0U)
        {
            obj->pressed_part = hit;
            obj->armed_part = hit;
        }
        else
        {
            obj->pressed_part = 0U;
            obj->armed_part = 0U;
        }
        break;

    case WE_EVENT_STAY:
        if (obj->dragging)
        {
            int16_t dy = (int16_t)(data->y - obj->drag_start_y);
            int16_t abs_dy = (dy >= 0) ? dy : (int16_t)(-dy);
            int16_t new_scroll = (int16_t)(obj->drag_start_scroll_y - dy);
            int16_t max_scroll = (int16_t)(popup->panel.content_h - _dropdown_panel_view_height(obj));
            if (max_scroll < 0)
                max_scroll = 0;
            if (new_scroll < 0)
                new_scroll = 0;
            if (new_scroll > max_scroll)
                new_scroll = max_scroll;
            if (abs_dy >= 3 && new_scroll != popup->panel.scroll_y)
            {
                popup->panel.scroll_y = new_scroll;
                obj->drag_moved = 1U;
                obj->pressed_part = 0U;
                _dropdown_invalidate_panel(obj);
            }
        }
        break;

    case WE_EVENT_RELEASED:
        obj->pressed_part = 0U;
        obj->dragging = 0U;
        break;

    case WE_EVENT_CLICKED:
        if (!obj->drag_moved)
        {
            if (obj->armed_part == hit && hit >= 2U)
            {
                uint8_t idx = (uint8_t)(hit - 2U);
                if (idx < obj->item_count)
                {
                    obj->selected_index = idx;
                    if (obj->user_event_cb != NULL)
                        obj->user_event_cb(obj, event, data);
                }
            }
            _dropdown_set_expanded_internal(obj, 0U);
        }
        obj->pressed_part = 0U;
        obj->armed_part = 0U;
        obj->dragging = 0U;
        obj->drag_moved = 0U;
        break;

    default:
        break;
    }

    if (old_expanded != obj->expanded || old_selected != obj->selected_index)
        _dropdown_invalidate_all(obj);
    else if (old_pressed != obj->pressed_part)
        _dropdown_invalidate_panel(obj);

    return 1U;
}

void we_dropdown_obj_init(we_dropdown_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, uint16_t w, uint16_t h,
                          const unsigned char *font,
                          const char * const *items, uint8_t item_count,
                          uint8_t init_index,
                          we_dropdown_event_cb_t event_cb)
{
    if (obj == NULL || lcd == NULL || w == 0U || h == 0U)
        return;

    obj->base.lcd = (struct we_lcd_t *)lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = (int16_t)w;
    obj->base.h = (int16_t)((h < WE_DROPDOWN_ROW_HEIGHT) ? WE_DROPDOWN_ROW_HEIGHT : h);
    obj->base.class_p = &_dropdown_class;
    obj->base.parent = NULL;
    obj->base.next = NULL;

    obj->popup.owner = obj;
    obj->popup.base.next = NULL;
    obj->popup.base.parent = NULL;
    obj->popup.base.class_p = NULL;
    obj->popup.base.lcd = NULL;
    obj->popup.base.x = 0;
    obj->popup.base.y = 0;
    obj->popup.base.w = 0;
    obj->popup.base.h = 0;
    obj->popup.panel.base.lcd = NULL;
    obj->popup.panel.base.class_p = NULL;
    obj->popup.panel.base.next = NULL;
    obj->popup.panel.base.parent = NULL;

    obj->font = font;
    obj->items = items;
    obj->item_count = item_count;
    obj->selected_index = 0U;
    obj->expanded = 0U;
    obj->pressed_part = 0U;
    obj->armed_part = 0U;
    obj->visible_rows = WE_DROPDOWN_VISIBLE_ROWS;
    obj->row_height = WE_DROPDOWN_ROW_HEIGHT;
    obj->show_scrollbar = 1U;
    obj->opacity = 255U;
    obj->dragging = 0U;
    obj->drag_moved = 0U;
    obj->drag_start_y = 0;
    obj->drag_start_scroll_y = 0;
    obj->user_event_cb = event_cb;

    obj->box_color = RGB888TODEV(248, 249, 252);
    obj->panel_color = RGB888TODEV(242, 244, 248);
    obj->border_color = RGB888TODEV(188, 194, 205);
    obj->text_color = RGB888TODEV(32, 38, 46);
    obj->hint_color = RGB888TODEV(96, 108, 124);
    obj->selected_color = RGB888TODEV(214, 228, 255);
    obj->pressed_color = RGB888TODEV(230, 236, 246);

    if (obj->item_count > 0U)
        obj->selected_index = _dropdown_clamp_index(obj, init_index);

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

    _dropdown_invalidate_main(obj);
}

void we_dropdown_obj_delete(we_dropdown_obj_t *obj)
{
    if (obj == NULL)
        return;
    _dropdown_set_expanded_internal(obj, 0U);
    we_obj_delete((we_obj_t *)obj);
}

void we_dropdown_set_items(we_dropdown_obj_t *obj,
                           const char * const *items, uint8_t item_count,
                           uint8_t selected_index)
{
    if (obj == NULL)
        return;

    _dropdown_set_expanded_internal(obj, 0U);
    obj->items = items;
    obj->item_count = item_count;
    obj->pressed_part = 0U;
    obj->armed_part = 0U;
    obj->selected_index = (item_count > 0U) ? _dropdown_clamp_index(obj, selected_index) : 0U;
    _dropdown_invalidate_all(obj);
}

void we_dropdown_set_selected(we_dropdown_obj_t *obj, uint8_t selected_index)
{
    uint8_t new_index;

    if (obj == NULL || obj->item_count == 0U)
        return;

    new_index = _dropdown_clamp_index(obj, selected_index);
    if (new_index == obj->selected_index)
        return;

    obj->selected_index = new_index;
    if (obj->expanded)
        _dropdown_sync_popup_scroll(obj);
    _dropdown_invalidate_all(obj);
}

uint8_t we_dropdown_get_selected(const we_dropdown_obj_t *obj)
{
    if (obj == NULL)
        return 0U;
    return obj->selected_index;
}

const char *we_dropdown_get_selected_text(const we_dropdown_obj_t *obj)
{
    if (obj == NULL || obj->items == NULL || obj->item_count == 0U)
        return NULL;
    return obj->items[_dropdown_clamp_index(obj, obj->selected_index)];
}

void we_dropdown_set_expanded(we_dropdown_obj_t *obj, uint8_t expanded)
{
    if (obj == NULL)
        return;
    _dropdown_set_expanded_internal(obj, expanded ? 1U : 0U);
    if (obj->expanded)
    {
        _dropdown_sync_popup_scroll(obj);
        we_obj_bring_to_front((we_obj_t *)&obj->popup);
    }
    _dropdown_invalidate_all(obj);
}

void we_dropdown_set_visible_rows(we_dropdown_obj_t *obj, uint8_t visible_rows)
{
    if (obj == NULL)
        return;
    if (visible_rows == 0U)
        visible_rows = 1U;
    if (obj->visible_rows == visible_rows)
        return;
    obj->visible_rows = visible_rows;
    if (obj->expanded)
        _dropdown_set_expanded_internal(obj, 1U);
    _dropdown_invalidate_all(obj);
}

void we_dropdown_set_popup_scrollbar(we_dropdown_obj_t *obj, uint8_t show_scrollbar)
{
    if (obj == NULL)
        return;
    obj->show_scrollbar = show_scrollbar ? 1U : 0U;
    _dropdown_invalidate_panel(obj);
}

void we_dropdown_set_opacity(we_dropdown_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL)
        return;
    if (obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    _dropdown_invalidate_all(obj);
}

void we_dropdown_set_colors(we_dropdown_obj_t *obj,
                            colour_t box_color,
                            colour_t panel_color,
                            colour_t border_color,
                            colour_t text_color,
                            colour_t hint_color,
                            colour_t selected_color,
                            colour_t pressed_color)
{
    if (obj == NULL)
        return;

    obj->box_color = box_color;
    obj->panel_color = panel_color;
    obj->border_color = border_color;
    obj->text_color = text_color;
    obj->hint_color = hint_color;
    obj->selected_color = selected_color;
    obj->pressed_color = pressed_color;
    _dropdown_invalidate_all(obj);
}

void we_dropdown_set_pos(we_dropdown_obj_t *obj, int16_t x, int16_t y)
{
    if (obj == NULL)
        return;
    if (obj->base.x == x && obj->base.y == y)
        return;
    _dropdown_invalidate_all(obj);
    obj->base.x = x;
    obj->base.y = y;
    _dropdown_invalidate_all(obj);
}
