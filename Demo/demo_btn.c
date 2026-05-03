/**
 * @file  demo_btn.c
 * @brief 按钮控件 (Button) 基础演示
 *
 * 演示内容：
 * 1. 四种静态状态：NORMAL / SELECTED / PRESSED / DISABLED
 * 2. DYN 按钮每 900ms 自动循环切换状态
 * 3. DYN 按钮圆角半径同步变化，便于观察 radius 效果
 * 4. 右上角实时 FPS
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/btn/we_widget_btn.h"
#include <string.h>

static we_label_obj_t btn_title;
static we_label_obj_t btn_note;
static we_label_obj_t btn_fps;
static we_btn_obj_t   btn_normal;
static we_btn_obj_t   btn_selected;
static we_btn_obj_t   btn_pressed;
static we_btn_obj_t   btn_dynamic;

static uint32_t btn_ticks_ms;
static uint32_t btn_fps_timer;
static uint32_t btn_last_frames;
static char     btn_fps_buf[16];

/**
 * @brief 初始化按钮控件演示场景
 * @param lcd GUI 运行时上下文
 */
void we_btn_simple_demo_init(we_lcd_t *lcd)
{
    int16_t margin_x = 10;
    int16_t title_y  = 10;
    int16_t note_y   = 32;
    int16_t fps_x    = 196;
    int16_t btn_w    = 92;
    int16_t btn_h    = 32;
    int16_t gap_x    = 20;
    int16_t row1_y   = 82;
    int16_t row2_y   = 130;
    int16_t col1_x   = 18;
    int16_t col2_x   = (int16_t)(col1_x + btn_w + gap_x);

    btn_ticks_ms    = 0U;
    btn_fps_timer   = 0U;
    btn_last_frames = 0U;
    memset(btn_fps_buf, 0, sizeof(btn_fps_buf));

    we_label_obj_init(&btn_title, lcd, margin_x, title_y,
                      "BTN DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&btn_note, lcd, margin_x, note_y,
                      "state / radius / text", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&btn_fps, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    we_btn_obj_init(&btn_normal, lcd, col1_x, row1_y, btn_w, btn_h,
                    "NORMAL", we_font_consolas_18, NULL);
    we_btn_obj_init(&btn_selected, lcd, col2_x, row1_y, btn_w, btn_h,
                    "SELECT", we_font_consolas_18, NULL);
    we_btn_set_state(&btn_selected, WE_BTN_STATE_SELECTED);

    we_btn_obj_init(&btn_pressed, lcd, col1_x, row2_y, btn_w, btn_h,
                    "PRESS", we_font_consolas_18, NULL);
    we_btn_set_state(&btn_pressed, WE_BTN_STATE_PRESSED);

    we_btn_obj_init(&btn_dynamic, lcd, col2_x, row2_y, btn_w, btn_h,
                    "DYN", we_font_consolas_18, NULL);
}

/**
 * @brief 按钮演示周期更新函数
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 */
void we_btn_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint8_t phase;

    if (lcd == NULL || ms_tick == 0U)
    {
        return;
    }

    btn_ticks_ms += ms_tick;
    phase = (uint8_t)((btn_ticks_ms / 900U) & 0x03U);

    we_btn_set_state(&btn_dynamic, (we_btn_state_t)phase);
    we_btn_set_radius(&btn_dynamic, (uint16_t)(6U + phase * 4U));

    switch (phase)
    {
    case 0:
        we_btn_set_text(&btn_dynamic, "NORMAL");
        break;
    case 1:
        we_btn_set_text(&btn_dynamic, "SELECT");
        break;
    case 2:
        we_btn_set_text(&btn_dynamic, "PRESS");
        break;
    default:
        we_btn_set_text(&btn_dynamic, "DISABLE");
        break;
    }

    we_demo_update_fps(lcd, &btn_fps, &btn_fps_timer,
                       &btn_last_frames, btn_fps_buf, ms_tick);
}
