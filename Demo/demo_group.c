#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/group/we_widget_group.h"
#include "widgets/btn/we_widget_btn.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t group_title;
static we_label_obj_t group_note;
static we_label_obj_t group_fps;
static we_label_obj_t group_stat;
static we_group_obj_t group_panel;
static we_label_obj_t group_panel_title;
static we_btn_obj_t   group_btn_a;
static we_btn_obj_t   group_btn_b;
static we_label_obj_t group_panel_tip;

static uint32_t group_ticks_ms;
static uint32_t group_fps_timer;
static uint32_t group_last_frames;
static char     group_fps_buf[16];
static char     group_stat_buf[24];

/**
 * @brief Group 内按钮事件回调，仅消费 CLICKED
 * @param obj 传入：触发事件的对象指针
 * @param event 传入：事件类型
 * @param data 传入：输入设备数据
 * @return 1 表示事件已消费，0 表示未消费
 */
static uint8_t _group_btn_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    (void)obj;
    (void)data;
    return (event == WE_EVENT_CLICKED) ? 1U : 0U;
}

/**
 * @brief 初始化 group demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_group_simple_demo_init(we_lcd_t *lcd)
{
    int16_t margin_x = 10;
    int16_t title_y  = 10;
    int16_t note_y   = 32;
    int16_t stat_y   = 54;
    int16_t fps_x    = 196;
    int16_t panel_x  = 14;
    int16_t panel_y  = 84;
    int16_t panel_w  = 252;
    int16_t panel_h  = 118;
    int16_t btn_w    = 92;
    int16_t btn_h    = 34;

    group_ticks_ms    = 0U;
    group_fps_timer   = 0U;
    group_last_frames = 0U;
    memset(group_fps_buf, 0, sizeof(group_fps_buf));
    memset(group_stat_buf, 0, sizeof(group_stat_buf));

    we_label_obj_init(&group_title, lcd, margin_x, title_y,
                      "GROUP DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&group_note, lcd, margin_x, note_y,
                      "relative child layout", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&group_fps, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&group_stat, lcd, margin_x, stat_y,
                      "GROUP X 000", we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    we_group_obj_init(&group_panel, lcd, panel_x, panel_y, panel_w, panel_h,
                      RGB888TODEV(24, 31, 43), 255);

    we_label_obj_init(&group_panel_title, lcd, 0, 0,
                      "PANEL", we_font_consolas_18,
                      RGB888TODEV(255, 154, 102), 255);
    we_btn_obj_init(&group_btn_a, lcd, 0, 0, btn_w, btn_h,
                    "LEFT", we_font_consolas_18, _group_btn_cb);
    we_btn_obj_init(&group_btn_b, lcd, 0, 0, btn_w, btn_h,
                    "RIGHT", we_font_consolas_18, _group_btn_cb);
    we_label_obj_init(&group_panel_tip, lcd, 0, 0,
                      "children follow group", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    we_group_add_child(&group_panel, (we_obj_t *)&group_panel_title);
    we_group_add_child(&group_panel, (we_obj_t *)&group_btn_a);
    we_group_add_child(&group_panel, (we_obj_t *)&group_btn_b);
    we_group_add_child(&group_panel, (we_obj_t *)&group_panel_tip);

    we_group_set_child_pos(&group_panel, (we_obj_t *)&group_panel_title, 12, 10);
    we_group_set_child_pos(&group_panel, (we_obj_t *)&group_btn_a, 14, 46);
    we_group_set_child_pos(&group_panel, (we_obj_t *)&group_btn_b, 146, 46);
    we_group_set_child_pos(&group_panel, (we_obj_t *)&group_panel_tip, 12, 92);
}

/**
 * @brief group demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_group_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    int16_t panel_x;

    if (lcd == NULL || ms_tick == 0U)
        return;

    group_ticks_ms += ms_tick;
    panel_x = (int16_t)(14 + ((we_sin((int16_t)((group_ticks_ms * 32U) / 100U) & 0x1FF) * 14) >> 15));

    we_obj_set_pos((we_obj_t *)&group_panel, panel_x, 84);
    we_group_relayout(&group_panel);

    snprintf(group_stat_buf, sizeof(group_stat_buf), "GROUP X %03d", (int)panel_x);
    we_label_set_text(&group_stat, group_stat_buf);

    we_demo_update_fps(lcd, &group_fps, &group_fps_timer,
                       &group_last_frames, group_fps_buf, ms_tick);
}
