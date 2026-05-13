/**
 * @file  demo_img.c
 * @brief 图片控件 (Image) 基础演示
 *
 * 演示内容：
 * 1. 三张图片叠放：主图、透明度呼吸叠加图、上下浮动图标
 * 2. 状态栏实时显示叠加图透明度
 * 3. 右上角 FPS
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "image_res.h"
#include "widgets/img/we_widget_img.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t img_title;
static we_label_obj_t img_note;
static we_label_obj_t img_fps;
static we_label_obj_t img_stat;
static we_img_obj_t   img_main;
static we_img_obj_t   img_overlay;
static we_img_obj_t   img_float;

static uint32_t img_ticks_ms;
static uint32_t img_fps_timer;
static uint32_t img_last_frames;
static char     img_fps_buf[16];
static char     img_stat_buf[24];

/**
 * @brief 初始化图片控件演示场景
 * @param lcd GUI 运行时上下文
 */
void we_img_simple_demo_init(we_lcd_t *lcd)
{
    int16_t margin_x = 10;
    int16_t title_y  = 10;
    int16_t note_y   = 32;
    int16_t stat_y   = 54;
    int16_t fps_x    = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    img_ticks_ms    = 0U;
    img_fps_timer   = 0U;
    img_last_frames = 0U;
    memset(img_fps_buf, 0, sizeof(img_fps_buf));
    memset(img_stat_buf, 0, sizeof(img_stat_buf));

    we_label_obj_init(&img_title, lcd, margin_x, title_y,
                      "IMG DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&img_note, lcd, margin_x, note_y,
                      "opacity / position", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&img_fps, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&img_stat, lcd, margin_x, stat_y,
                      "IMG OP 255", we_font_consolas_18,
                      RGB888TODEV(255, 154, 102), 255);

    we_img_obj_init(&img_main, lcd, 18, 82, img_rgb565_indexqoi_96x54, 255);
    we_img_obj_init(&img_overlay, lcd, 30, 108, img_argb8565_indexqoi_208x42, 200);
    we_img_obj_init(&img_float, lcd, 168, 70, img_argb8565_indexqoi_80x80, 200);
}

/**
 * @brief 图片演示周期更新函数
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 */
void we_img_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint8_t opacity;
    int16_t float_y;

    if (lcd == NULL || ms_tick == 0U)
    {
        return;
    }

    img_ticks_ms += ms_tick;

    opacity = (uint8_t)(90U + (((uint32_t)(we_sin((int16_t)((img_ticks_ms * 6U) / 100U) & 0x1FF))
                                + 32768U) * 130U >> 16));
    float_y = (int16_t)(70 + ((we_sin((int16_t)((img_ticks_ms * 7U) / 100U) & 0x1FF) * 10) >> 15));

    we_img_obj_set_opacity(&img_overlay, opacity);
    we_img_obj_set_pos(&img_float, 168, float_y);

    snprintf(img_stat_buf, sizeof(img_stat_buf), "IMG OP %03u", (unsigned)opacity);
    we_label_set_text(&img_stat, img_stat_buf);

    we_demo_update_fps(lcd, &img_fps, &img_fps_timer,
                       &img_last_frames, img_fps_buf, ms_tick);
}
