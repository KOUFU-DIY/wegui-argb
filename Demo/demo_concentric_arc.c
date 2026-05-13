/**
 * @file  demo_concentric_arc.c
 * @brief 同心圆弧演示 (三环进度条)
 *
 * 演示内容：
 * 1. 三个同心圆弧共享同一圆心
 * 2. 三组数值按不同相位同步变化
 * 3. 状态行实时显示三弧当前值
 * 4. 右上角 FPS
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/arc/we_widget_arc.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t concentric_arc_title;
static we_label_obj_t concentric_arc_note;
static we_label_obj_t concentric_arc_fps;
static we_label_obj_t concentric_arc_stat;
static we_arc_obj_t   concentric_arc_outer;
static we_arc_obj_t   concentric_arc_mid;
static we_arc_obj_t   concentric_arc_inner;

static uint32_t concentric_arc_ticks_ms;
static uint32_t concentric_arc_fps_timer;
static uint32_t concentric_arc_last_frames;
static char     concentric_arc_fps_buf[16];
static char     concentric_arc_stat_buf[32];

/**
 * @brief 将正弦相位值映射到 0~255 区间
 * @param angle 512 步进角度值
 * @return 映射后的 8 位值
 */
static uint8_t _sin_to_u8(int16_t angle)
{
    return (uint8_t)(((uint32_t)(we_sin(angle) + 32768)) >> 8);
}

/**
 * @brief 初始化同心圆弧演示场景
 * @param lcd GUI 运行时上下文
 */
void we_concentric_arc_simple_demo_init(we_lcd_t *lcd)
{
    int16_t margin_x = 10;
    int16_t title_y  = 10;
    int16_t note_y   = 32;
    int16_t stat_y   = 54;
    int16_t fps_x    = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    int16_t center_x = (int16_t)(lcd->width / 2);
    int16_t center_y = 142;

    concentric_arc_ticks_ms    = 0U;
    concentric_arc_fps_timer   = 0U;
    concentric_arc_last_frames = 0U;
    memset(concentric_arc_fps_buf, 0, sizeof(concentric_arc_fps_buf));
    memset(concentric_arc_stat_buf, 0, sizeof(concentric_arc_stat_buf));

    we_label_obj_init(&concentric_arc_title, lcd, margin_x, title_y,
                      "CONCENTRIC ARC", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&concentric_arc_note, lcd, margin_x, note_y,
                      "three rings / one center", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&concentric_arc_fps, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&concentric_arc_stat, lcd, margin_x, stat_y,
                      "O 000  M 000  I 000", we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    we_arc_obj_init(&concentric_arc_outer, lcd, center_x, center_y,
                    64, 10,
                    WE_DEG(135), WE_DEG(405),
                    RGB888TODEV(255, 154, 102), RGB888TODEV(46, 58, 72), 128);
    we_arc_obj_init(&concentric_arc_mid, lcd, center_x, center_y,
                    46, 10,
                    WE_DEG(135), WE_DEG(405),
                    RGB888TODEV(120, 230, 205), RGB888TODEV(40, 52, 64), 180);
    we_arc_obj_init(&concentric_arc_inner, lcd, center_x, center_y,
                    28, 10,
                    WE_DEG(135), WE_DEG(405),
                    RGB888TODEV(196, 170, 255), RGB888TODEV(36, 44, 58), 180);
}

/**
 * @brief 同心圆弧演示周期更新函数
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 */
void we_concentric_arc_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint8_t outer_value;
    uint8_t mid_value;
    uint8_t inner_value;
    int16_t t;

    if (lcd == NULL || ms_tick == 0U)
        return;

    concentric_arc_ticks_ms += ms_tick;
    t = (int16_t)((concentric_arc_ticks_ms * 5U / 100U) & 0x1FFU);

    outer_value = _sin_to_u8(t);
    mid_value   = _sin_to_u8((int16_t)((t + 128) & 0x1FF));
    inner_value = _sin_to_u8((int16_t)((t + 256) & 0x1FF));

    we_arc_set_value(&concentric_arc_outer, outer_value);
    we_arc_set_value(&concentric_arc_mid, mid_value);
    we_arc_set_value(&concentric_arc_inner, inner_value);

    sprintf(concentric_arc_stat_buf, "O %03u  M %03u  I %03u",
            (unsigned)outer_value, (unsigned)mid_value, (unsigned)inner_value);
    we_label_set_text(&concentric_arc_stat, concentric_arc_stat_buf);

    we_demo_update_fps(lcd, &concentric_arc_fps,
                       &concentric_arc_fps_timer, &concentric_arc_last_frames,
                       concentric_arc_fps_buf, ms_tick);
}
