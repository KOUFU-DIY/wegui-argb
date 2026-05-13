/**
 * @file  demo_img_ex.c
 * @brief 高级图片控件 (Image Ex) 基础演示
 *
 * 演示内容：
 * 1. 图片绕偏移 pivot 摆动旋转
 * 2. 图片同时做等比例缩放呼吸
 * 3. 状态行实时显示当前角度和缩放值
 * 4. 右上角 FPS
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "image_res.h"
#include "widgets/img_ex/we_widget_img_ex.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t  img_ex_title;
static we_label_obj_t  img_ex_note;
static we_label_obj_t  img_ex_fps;
static we_label_obj_t  img_ex_stat;
static we_img_ex_obj_t img_ex_obj;

static uint32_t img_ex_ticks_ms;
static uint32_t img_ex_fps_timer;
static uint32_t img_ex_last_frames;
static char     img_ex_fps_buf[16];
static char     img_ex_stat_buf[32];

/**
 * @brief 初始化高级图片控件演示场景
 * @param lcd GUI 运行时上下文
 */
void we_img_ex_simple_demo_init(we_lcd_t *lcd)
{
    int16_t margin_x = 10;
    int16_t title_y  = 10;
    int16_t note_y   = 32;
    int16_t stat_y   = 54;
    int16_t fps_x    = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    img_ex_ticks_ms    = 0U;
    img_ex_fps_timer   = 0U;
    img_ex_last_frames = 0U;
    memset(img_ex_fps_buf, 0, sizeof(img_ex_fps_buf));
    memset(img_ex_stat_buf, 0, sizeof(img_ex_stat_buf));

    we_label_obj_init(&img_ex_title, lcd, margin_x, title_y,
                      "IMG_EX DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&img_ex_note, lcd, margin_x, note_y,
                      "angle / scale / pivot", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&img_ex_fps, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&img_ex_stat, lcd, margin_x, stat_y,
                      "ANG 0  SCALE 256", we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    we_img_ex_obj_init(&img_ex_obj, lcd, 160, 148, img_rgb565_64x80, 255);
    we_img_ex_obj_set_pivot_offset(&img_ex_obj, -44, 0);
}

/**
 * @brief 高级图片演示周期更新函数
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 */
void we_img_ex_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    int16_t  angle;
    int32_t  swing_q15;
    int32_t  scale_q15;
    uint16_t scale_256;

    if (lcd == NULL || ms_tick == 0U)
    {
        return;
    }

    img_ex_ticks_ms += ms_tick;

    swing_q15 = we_sin((int16_t)((img_ex_ticks_ms * 8U) / 100U) & 0x1FF);
    angle = (int16_t)((swing_q15 * WE_DEG(105)) >> 15);

    scale_q15 = we_sin((int16_t)((img_ex_ticks_ms * 12U) / 100U) & 0x1FF);
    scale_256 = (uint16_t)(256 + ((scale_q15 * 32) >> 15));

    we_img_ex_obj_set_transform(&img_ex_obj, angle, scale_256);

    sprintf(img_ex_stat_buf, "ANG %d  SCALE %u", (int)angle, (unsigned)scale_256);
    we_label_set_text(&img_ex_stat, img_ex_stat_buf);

    we_demo_update_fps(lcd, &img_ex_fps, &img_ex_fps_timer,
                       &img_ex_last_frames, img_ex_fps_buf, ms_tick);
}
