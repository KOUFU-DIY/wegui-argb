#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/btn/we_widget_btn.h"
#include "widgets/label/we_widget_label.h"
#include "widgets/scroll_panel/we_widget_scroll_panel.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t demo_title;
static we_label_obj_t demo_hint;
static we_label_obj_t demo_fps;
static we_label_obj_t demo_stat;

static we_scroll_panel_obj_t demo_panel;
static we_label_obj_t panel_header;
static we_label_obj_t panel_item_a;
static we_label_obj_t panel_item_b;
static we_label_obj_t panel_item_c;
static we_label_obj_t panel_item_d;
static we_label_obj_t panel_item_e;
static we_label_obj_t panel_item_f;
static we_btn_obj_t panel_btn_a;
static we_btn_obj_t panel_btn_b;
static we_label_obj_t panel_item_g;
static we_label_obj_t panel_item_h;

static uint32_t demo_fps_timer;
static uint32_t demo_last_frames;
static char demo_fps_buf[16];
static char demo_stat_buf[24];
static int16_t demo_last_scroll_y;

static void _scroll_panel_demo_update_stat(void)
{
    sprintf(demo_stat_buf, "SCROLL %03d", (int)demo_panel.scroll_y);
    we_label_set_text(&demo_stat, demo_stat_buf);
}

static uint8_t _scroll_panel_demo_ctrl_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    we_btn_obj_t *btn = (we_btn_obj_t *)obj;
    (void)data;

    if (btn == &panel_btn_a)
    {
        switch (event)
        {
        case WE_EVENT_PRESSED:
            we_btn_set_state(btn, WE_BTN_STATE_PRESSED);
            break;
        case WE_EVENT_RELEASED:
            we_btn_set_state(btn, WE_BTN_STATE_NORMAL);
            break;
        case WE_EVENT_CLICKED:
            we_btn_set_text(btn, "CLICKED");
            return 1U;
        default:
            break;
        }
        return 1U;
    }

    if (btn == &panel_btn_b)
    {
        switch (event)
        {
        case WE_EVENT_PRESSED:
            we_btn_set_state(btn, WE_BTN_STATE_PRESSED);
            break;
        case WE_EVENT_RELEASED:
            we_btn_set_state(btn, WE_BTN_STATE_NORMAL);
            break;
        case WE_EVENT_CLICKED:
            we_btn_set_text(btn, "ACTIVE");
            return 1U;
        default:
            break;
        }
        return 1U;
    }

    return 0U;
}

void we_scroll_panel_simple_demo_init(we_lcd_t *lcd)
{
    int16_t margin_x = 10;
    int16_t title_y = 10;
    int16_t hint_y = 32;
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    int16_t stat_y = 54;
    int16_t panel_x = 14;
    int16_t panel_y = 82;
    int16_t panel_w = 252;
    int16_t panel_h = 132;

    demo_fps_timer = 0U;
    demo_last_frames = 0U;
    demo_last_scroll_y = -32768;
    memset(demo_fps_buf, 0, sizeof(demo_fps_buf));
    memset(demo_stat_buf, 0, sizeof(demo_stat_buf));

    we_label_obj_init(&demo_title, lcd, margin_x, title_y,
                      "SCROLL PANEL", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&demo_hint, lcd, margin_x, hint_y,
                      "drag with child buttons inside", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&demo_fps, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    strcpy(demo_stat_buf, "SCROLL 000");
    we_label_obj_init(&demo_stat, lcd, margin_x, stat_y,
                      demo_stat_buf, we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    we_scroll_panel_obj_init(&demo_panel, lcd, panel_x, panel_y, panel_w, panel_h,
                             RGB888TODEV(20, 27, 38), RGB888TODEV(58, 66, 82),
                             0U, 255);
    we_scroll_panel_set_scrollbar(&demo_panel, 1U, 6U);
    we_scroll_panel_set_scrollbar_opacity(&demo_panel, 180U);
    we_scroll_panel_set_content_h(&demo_panel, 316);

    we_label_obj_init(&panel_header, lcd, 0, 0,
                      "SCROLL AREA", we_font_consolas_18,
                      RGB888TODEV(255, 154, 102), 255);
    we_label_obj_init(&panel_item_a, lcd, 0, 0,
                      "Item 01: viewport clip", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&panel_item_b, lcd, 0, 0,
                      "Item 02: child labels", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&panel_item_c, lcd, 0, 0,
                      "Item 03: local positions", we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);
    we_label_obj_init(&panel_item_d, lcd, 0, 0,
                      "Item 04: inertia + rebound", we_font_consolas_18,
                      RGB888TODEV(255, 191, 122), 255);
    we_label_obj_init(&panel_item_e, lcd, 0, 0,
                      "Item 05: optional scrollbar", we_font_consolas_18,
                      RGB888TODEV(177, 220, 255), 255);
    we_label_obj_init(&panel_item_f, lcd, 0, 0,
                      "Item 06: child buttons work", we_font_consolas_18,
                      RGB888TODEV(200, 210, 222), 255);
    we_btn_obj_init(&panel_btn_a, lcd, 0, 0, 92, 32,
                    "PANEL A", we_font_consolas_18, _scroll_panel_demo_ctrl_cb);
    we_btn_obj_init(&panel_btn_b, lcd, 0, 0, 92, 32,
                    "PANEL B", we_font_consolas_18, _scroll_panel_demo_ctrl_cb);
    we_label_obj_init(&panel_item_g, lcd, 0, 0,
                      "Item 07: drag with inertia", we_font_consolas_18,
                      RGB888TODEV(160, 236, 168), 255);
    we_label_obj_init(&panel_item_h, lcd, 0, 0,
                      "Item 08: top/bottom rebound", we_font_consolas_18,
                      RGB888TODEV(255, 164, 186), 255);

    we_scroll_panel_add_child(&demo_panel, (we_obj_t *)&panel_header);
    we_scroll_panel_add_child(&demo_panel, (we_obj_t *)&panel_item_a);
    we_scroll_panel_add_child(&demo_panel, (we_obj_t *)&panel_item_b);
    we_scroll_panel_add_child(&demo_panel, (we_obj_t *)&panel_item_c);
    we_scroll_panel_add_child(&demo_panel, (we_obj_t *)&panel_item_d);
    we_scroll_panel_add_child(&demo_panel, (we_obj_t *)&panel_item_e);
    we_scroll_panel_add_child(&demo_panel, (we_obj_t *)&panel_item_f);
    we_scroll_panel_add_child(&demo_panel, (we_obj_t *)&panel_btn_a);
    we_scroll_panel_add_child(&demo_panel, (we_obj_t *)&panel_btn_b);
    we_scroll_panel_add_child(&demo_panel, (we_obj_t *)&panel_item_g);
    we_scroll_panel_add_child(&demo_panel, (we_obj_t *)&panel_item_h);

    we_scroll_panel_set_child_pos(&demo_panel, (we_obj_t *)&panel_header, 10, 10);
    we_scroll_panel_set_child_pos(&demo_panel, (we_obj_t *)&panel_item_a, 10, 40);
    we_scroll_panel_set_child_pos(&demo_panel, (we_obj_t *)&panel_item_b, 10, 74);
    we_scroll_panel_set_child_pos(&demo_panel, (we_obj_t *)&panel_item_c, 10, 108);
    we_scroll_panel_set_child_pos(&demo_panel, (we_obj_t *)&panel_item_d, 10, 142);
    we_scroll_panel_set_child_pos(&demo_panel, (we_obj_t *)&panel_item_e, 10, 176);
    we_scroll_panel_set_child_pos(&demo_panel, (we_obj_t *)&panel_item_f, 10, 210);
    we_scroll_panel_set_child_pos(&demo_panel, (we_obj_t *)&panel_btn_a, 10, 238);
    we_scroll_panel_set_child_pos(&demo_panel, (we_obj_t *)&panel_btn_b, 130, 238);
    we_scroll_panel_set_child_pos(&demo_panel, (we_obj_t *)&panel_item_g, 10, 282);
    we_scroll_panel_set_child_pos(&demo_panel, (we_obj_t *)&panel_item_h, 10, 316);
}

void we_scroll_panel_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    if (demo_last_scroll_y != demo_panel.scroll_y)
    {
        demo_last_scroll_y = demo_panel.scroll_y;
        _scroll_panel_demo_update_stat();
    }

    we_demo_update_fps(lcd, &demo_fps, &demo_fps_timer,
                       &demo_last_frames, demo_fps_buf, ms_tick);
}
