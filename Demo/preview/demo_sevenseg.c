/**
 * @file  demo_sevenseg.c
 * @brief 七段数码管（sevenseg）preview demo —— DEMO_ID 117
 *
 * 演示内容：
 * 1. 大号 "12:34" 时钟：每秒分钟 +1 翻动，ghost 鬼影开启（可见灭段底纹）
 * 2. 下方小号 4 位计数器：每 80ms 自增，ghost 关闭 + 橙色配色
 * 3. 两种 digit_h 尺寸对比（64 / 28），右上角 FPS
 */

#include "preview_demos.h"

#include "demo_common.h"
#include "widgets_preview/sevenseg/we_widget_sevenseg.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t    ss_title;
static we_label_obj_t    ss_note;
static we_label_obj_t    ss_fps;
static we_sevenseg_obj_t ss_clock;   /* 大号时钟（5 位含冒号） */
static we_sevenseg_obj_t ss_count;   /* 小号计数器（4 位） */

static uint32_t ss_fps_timer;
static uint32_t ss_last_frames;
static char     ss_fps_buf[16];

static uint32_t ss_clock_acc;        /* 时钟累计毫秒 */
static uint32_t ss_count_acc;        /* 计数器累计毫秒 */
static uint8_t  ss_hour;
static uint8_t  ss_minute;
static uint16_t ss_counter;
static char     ss_clock_buf[8];     /* "HH:MM" 静态缓冲（调用方持有） */
static char     ss_count_buf[8];     /* "NNNN" 静态缓冲 */

/**
 * @brief 初始化 sevenseg preview demo 场景
 * @param lcd GUI 运行时上下文
 * @return 无
 */
void we_sevenseg_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    ss_fps_timer   = 0U;
    ss_last_frames = 0U;
    ss_clock_acc   = 0U;
    ss_count_acc   = 0U;
    ss_hour        = 12U;
    ss_minute      = 34U;
    ss_counter     = 0U;
    memset(ss_fps_buf, 0, sizeof(ss_fps_buf));
    memset(ss_clock_buf, 0, sizeof(ss_clock_buf));
    memset(ss_count_buf, 0, sizeof(ss_count_buf));

    we_label_obj_init(&ss_title, lcd, 10, 10,
                      "SEVENSEG PREVIEW", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&ss_note, lcd, 10, 32,
                      "clock ghost on / counter off", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&ss_fps, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 大号时钟：digit_h=64 → 段厚 8、字宽 32、总宽 5*32+4*8=192，水平居中 */
    we_sevenseg_obj_init(&ss_clock, lcd, 44, 62, 64U, 5U);
    we_sevenseg_set_ghost(&ss_clock, 1U);
    snprintf(ss_clock_buf, sizeof(ss_clock_buf), "%02u:%02u",
             (unsigned)ss_hour, (unsigned)ss_minute);
    we_sevenseg_set_text(&ss_clock, ss_clock_buf);

    /* 小号计数器：digit_h=28 → 段厚 3、字宽 14、总宽 4*14+3*3=65，水平居中 */
    we_sevenseg_obj_init(&ss_count, lcd, 108, 160, 28U, 4U);
    we_sevenseg_set_colors(&ss_count,
                           RGB888TODEV(255, 154, 102),
                           RGB888TODEV(70, 62, 58));
    snprintf(ss_count_buf, sizeof(ss_count_buf), "%04u", (unsigned)ss_counter);
    we_sevenseg_set_text(&ss_count, ss_count_buf);
}

/**
 * @brief sevenseg preview demo 周期更新
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 * @return 无
 */
void we_sevenseg_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    /* 时钟：每 1000ms 分钟 +1 翻动 */
    ss_clock_acc += ms_tick;
    while (ss_clock_acc >= 1000U)
    {
        ss_clock_acc -= 1000U;
        ss_minute++;
        if (ss_minute >= 60U)
        {
            ss_minute = 0U;
            ss_hour   = (uint8_t)((ss_hour + 1U) % 24U);
        }
    }
    snprintf(ss_clock_buf, sizeof(ss_clock_buf), "%02u:%02u",
             (unsigned)ss_hour, (unsigned)ss_minute);
    we_sevenseg_set_text(&ss_clock, ss_clock_buf); /* 内容变才重绘 */

    /* 计数器：每 80ms 自增，展示快速刷新 */
    ss_count_acc += ms_tick;
    while (ss_count_acc >= 80U)
    {
        ss_count_acc -= 80U;
        ss_counter = (uint16_t)((ss_counter + 1U) % 10000U);
    }
    snprintf(ss_count_buf, sizeof(ss_count_buf), "%04u", (unsigned)ss_counter);
    we_sevenseg_set_text(&ss_count, ss_count_buf);

    we_demo_update_fps(lcd, &ss_fps, &ss_fps_timer,
                       &ss_last_frames, ss_fps_buf, ms_tick);
}
