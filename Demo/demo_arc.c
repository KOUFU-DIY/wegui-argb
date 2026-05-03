/**
 * @file  demo_arc.c
 * @brief 圆弧进度条 (Arc) 基础演示
 *
 * 演示内容：
 * 1. 两个同心圆弧同步变化
 * 2. 内圆弧透明度呼吸变化
 * 3. 状态行实时显示当前弧值
 * 4. 右上角 FPS
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/arc/we_widget_arc.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t arc_title;
static we_label_obj_t arc_note;
static we_label_obj_t arc_fps;
static we_label_obj_t arc_stat;
static we_arc_obj_t   arc_main;
static we_arc_obj_t   arc_sub;

static uint32_t arc_ticks_ms;
static uint32_t arc_fps_timer;
static uint32_t arc_last_frames;
static char     arc_fps_buf[16];
static char     arc_stat_buf[24];

/**
 * @brief 初始化圆弧控件演示场景
 * @param lcd GUI 运行时上下文
 */
void we_arc_simple_demo_init(we_lcd_t *lcd)
{
    int16_t margin_x = 10;
    int16_t title_y  = 10;
    int16_t note_y   = 32;
    int16_t stat_y   = 54;
    int16_t fps_x    = 196;
    int16_t center_x = (int16_t)(lcd->width / 2);
    int16_t center_y = 140;

    arc_ticks_ms    = 0U;
    arc_fps_timer   = 0U;
    arc_last_frames = 0U;
    memset(arc_fps_buf, 0, sizeof(arc_fps_buf));
    memset(arc_stat_buf, 0, sizeof(arc_stat_buf));

    we_label_obj_init(&arc_title, lcd, margin_x, title_y,
                      "ARC DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&arc_note, lcd, margin_x, note_y,
                      "value / opacity", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&arc_fps, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&arc_stat, lcd, margin_x, stat_y,
                      "VAL 000", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    we_arc_obj_init(&arc_main, lcd, center_x, center_y,
                    52, 14,
                    WE_DEG(135), WE_DEG(405),
                    RGB888TODEV(255, 154, 102), RGB888TODEV(50, 60, 76), 255);

    we_arc_obj_init(&arc_sub, lcd, center_x, center_y,
                    32, 8,
                    WE_DEG(225), WE_DEG(495),
                    RGB888TODEV(120, 230, 205), RGB888TODEV(30, 40, 52), 180);
}

/**
 * @brief 圆弧演示周期更新函数
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 */
void we_arc_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint8_t value;
    uint8_t opacity;

    if (lcd == NULL || ms_tick == 0U)
    {
        return;
    }

    arc_ticks_ms += ms_tick;

    value = (uint8_t)(((uint32_t)(we_sin((int16_t)((arc_ticks_ms * 5U) / 100U) & 0x1FF))
                        + 32768U) >> 8);
    opacity = (uint8_t)(110U + (((uint32_t)(we_sin((int16_t)((arc_ticks_ms * 4U) / 100U) & 0x1FF))
                                  + 32768U) * 110U >> 16));

    we_arc_set_value(&arc_main, value);
    we_arc_set_value(&arc_sub, (uint8_t)(255U - value));
    we_arc_set_opacity(&arc_sub, opacity);

    snprintf(arc_stat_buf, sizeof(arc_stat_buf), "VAL %03u", (unsigned)value);
    we_label_set_text(&arc_stat, arc_stat_buf);

    we_demo_update_fps(lcd, &arc_fps, &arc_fps_timer,
                       &arc_last_frames, arc_fps_buf, ms_tick);
}
