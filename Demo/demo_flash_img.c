/**
 * @file  demo_flash_img.c
 * @brief 外挂 Flash 图片控件演示
 *
 * 演示内容：
 * 1. 同时显示 RAW / INDEXQOI / ARGB8565 三种外挂图
 * 2. 两张动态图做透明度呼吸
 * 3. 状态行实时显示透明度与格式标签
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/img_flash/we_widget_img_flash.h"
#include "merged_bin.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t     fl_title;
static we_label_obj_t     fl_note;
static we_label_obj_t     fl_fps;
static we_label_obj_t     fl_stat;
static we_label_obj_t     fl_fmt;
static we_flash_img_obj_t fl_img1;
static we_flash_img_obj_t fl_img2;
static we_flash_img_obj_t fl_img3;

static uint32_t fl_ticks_ms;
static uint32_t fl_fps_timer;
static uint32_t fl_last_frames;
static char     fl_fps_buf[16];
static char     fl_stat_buf[28];

/**
 * @brief 初始化外挂 Flash 图片演示场景
 * @param lcd GUI 运行时上下文
 */
void we_flash_img_simple_demo_init(we_lcd_t *lcd)
{
    uint8_t ok = 0;
    const char *fmt_str;

    fl_ticks_ms    = 0U;
    fl_fps_timer   = 0U;
    fl_last_frames = 0U;
    memset(fl_fps_buf, 0, sizeof(fl_fps_buf));
    memset(fl_stat_buf, 0, sizeof(fl_stat_buf));

    we_label_obj_init(&fl_title, lcd, 10, 10,
                      "FLASH IMG DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&fl_note, lcd, 10, 32,
                      "raw / qoi / argb", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&fl_fps, lcd, we_demo_fps_x(lcd, "FPS", we_font_consolas_18), 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&fl_stat, lcd, 10, 54,
                      "FLASH OP 255", we_font_consolas_18,
                      RGB888TODEV(255, 154, 102), 255);

    ok |= we_flash_img_obj_init(&fl_img1, lcd, 56, 90, bin_addr_table[IMG_DEMO_RGB565_ID], 255U);
    ok |= we_flash_img_obj_init(&fl_img2, lcd, 128, 48, bin_addr_table[IMG_DEMO_RGB565_INDEXQOI2_ID], 255U);
    ok |= we_flash_img_obj_init(&fl_img3, lcd, 10, 130, bin_addr_table[IMG_DEMO_ARGB8565_INDEXQOI_ID], 255U);

    if (!ok)
    {
        fmt_str = "FMT: N/A";
    }
    else if (fl_img3.fmt == IMG_ARGB8565_INDEXQOI)
    {
        fmt_str = "FMT: ARGB";
    }
    else if (fl_img3.fmt == IMG_RGB565_INDEXQOI)
    {
        fmt_str = "FMT: QOI";
    }
    else
    {
        fmt_str = "FMT: RAW";
    }

    we_label_obj_init(&fl_fmt, lcd, 176, 54,
                      fmt_str, we_font_consolas_18,
                      RGB888TODEV(130, 200, 255), 255);

    if (!ok)
    {
        we_label_set_text(&fl_stat, "FLASH N/A");
    }
}

/**
 * @brief 外挂 Flash 图片演示周期更新函数
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 */
void we_flash_img_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint8_t opacity;
    uint8_t op3;

    if (lcd == NULL || ms_tick == 0U)
        return;

    fl_ticks_ms += ms_tick;

    opacity = (uint8_t)(90U + (((uint32_t)(we_sin((int16_t)((fl_ticks_ms * 25U) / 100U) & 0x1FF))
                                + 32768U) * 130U >> 16));
    we_flash_img_obj_set_opacity(&fl_img2, opacity);

    op3 = (uint8_t)(90U + (((uint32_t)(we_sin((int16_t)(((fl_ticks_ms * 25U) / 100U) + 128U) & 0x1FF))
                            + 32768U) * 130U >> 16));
    we_flash_img_obj_set_opacity(&fl_img3, op3);

    snprintf(fl_stat_buf, sizeof(fl_stat_buf), "FLASH OP %03u", (unsigned)opacity);
    we_label_set_text(&fl_stat, fl_stat_buf);

    we_demo_update_fps(lcd, &fl_fps, &fl_fps_timer,
                       &fl_last_frames, fl_fps_buf, ms_tick);
}
