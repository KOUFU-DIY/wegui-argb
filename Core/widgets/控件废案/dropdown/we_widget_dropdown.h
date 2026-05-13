#ifndef WE_WIDGET_DROPDOWN_H
#define WE_WIDGET_DROPDOWN_H

#include "we_gui_driver.h"
#include "widgets/scroll_panel/we_widget_scroll_panel.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef WE_DROPDOWN_RADIUS
#define WE_DROPDOWN_RADIUS 6U
#endif

#ifndef WE_DROPDOWN_ROW_HEIGHT
#define WE_DROPDOWN_ROW_HEIGHT 28U
#endif

#ifndef WE_DROPDOWN_VISIBLE_ROWS
#define WE_DROPDOWN_VISIBLE_ROWS 5U
#endif

#ifndef WE_DROPDOWN_ARROW_W
#define WE_DROPDOWN_ARROW_W 22U
#endif

struct we_dropdown_obj_s;
typedef uint8_t (*we_dropdown_event_cb_t)(void *obj, we_event_t event, we_indev_data_t *data);

typedef struct we_dropdown_popup_obj_s
{
    we_obj_t base;
    struct we_dropdown_obj_s *owner;
    we_scroll_panel_obj_t panel;
} we_dropdown_popup_obj_t;

typedef struct we_dropdown_obj_s
{
    we_obj_t base;
    we_dropdown_popup_obj_t popup;
    const unsigned char *font;
    const char * const *items;
    we_dropdown_event_cb_t user_event_cb;
    uint8_t item_count;
    uint8_t selected_index;
    uint8_t expanded;
    uint8_t pressed_part;   /* 0=none, 1=main box, 2+n=item n */
    uint8_t armed_part;
    uint8_t visible_rows;
    uint8_t row_height;
    uint8_t show_scrollbar;
    uint8_t opacity;
    uint8_t dragging;
    uint8_t drag_moved;
    int16_t drag_start_y;
    int16_t drag_start_scroll_y;
    colour_t box_color;
    colour_t panel_color;
    colour_t border_color;
    colour_t text_color;
    colour_t hint_color;
    colour_t selected_color;
    colour_t pressed_color;
} we_dropdown_obj_t;

void we_dropdown_obj_init(we_dropdown_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, uint16_t w, uint16_t h,
                          const unsigned char *font,
                          const char * const *items, uint8_t item_count,
                          uint8_t init_index,
                          we_dropdown_event_cb_t event_cb);

void we_dropdown_obj_delete(we_dropdown_obj_t *obj);

void we_dropdown_set_items(we_dropdown_obj_t *obj,
                           const char * const *items, uint8_t item_count,
                           uint8_t selected_index);

void we_dropdown_set_selected(we_dropdown_obj_t *obj, uint8_t selected_index);

uint8_t we_dropdown_get_selected(const we_dropdown_obj_t *obj);

const char *we_dropdown_get_selected_text(const we_dropdown_obj_t *obj);

void we_dropdown_set_expanded(we_dropdown_obj_t *obj, uint8_t expanded);
void we_dropdown_set_visible_rows(we_dropdown_obj_t *obj, uint8_t visible_rows);
void we_dropdown_set_popup_scrollbar(we_dropdown_obj_t *obj, uint8_t show_scrollbar);

void we_dropdown_set_opacity(we_dropdown_obj_t *obj, uint8_t opacity);

void we_dropdown_set_colors(we_dropdown_obj_t *obj,
                            colour_t box_color,
                            colour_t panel_color,
                            colour_t border_color,
                            colour_t text_color,
                            colour_t hint_color,
                            colour_t selected_color,
                            colour_t pressed_color);

void we_dropdown_set_pos(we_dropdown_obj_t *obj, int16_t x, int16_t y);

#ifdef __cplusplus
}
#endif

#endif
