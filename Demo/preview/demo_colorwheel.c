/**
 * @file  demo_colorwheel.c
 * @brief HSV 色轮（colorwheel）preview demo —— DEMO_ID 119
 *
 * 演示内容：
 * 1. 左侧 156px 色轮，按下 / 拖动实时选色
 * 2. 右侧大色块 box 实时跟随选中色
 * 3. 三行 label 显示 R / G / B 数值（整数 sprintf 静态缓冲）
 * 4. 右上角 FPS
 */

#include "preview_demos.h"

#include "demo_common.h"
#include "widgets/box/we_widget_box.h"
#include "widgets_preview/colorwheel/we_widget_colorwheel.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t      cw_title;
static we_label_obj_t      cw_note;
static we_label_obj_t      cw_fps;
static we_colorwheel_obj_t cw_wheel;
static we_box_obj_t        cw_swatch;   /* 右侧选中色大色块 */
static we_label_obj_t      cw_lbl_r;
static we_label_obj_t      cw_lbl_g;
static we_label_obj_t      cw_lbl_b;

static uint32_t cw_fps_timer;
static uint32_t cw_last_frames;
static char     cw_fps_buf[16];
static char     cw_r_buf[8];
static char     cw_g_buf[8];
static char     cw_b_buf[8];

/**
 * @brief 把当前选中色同步到右侧色块与 R/G/B 数值标签
 * @param wheel 色轮控件指针
 * @return 无
 */
static void _cw_demo_sync(we_colorwheel_obj_t *wheel)
{
    uint8_t r, g, b;

    we_colorwheel_get_rgb(wheel, &r, &g, &b);
    we_box_set_color(&cw_swatch, we_colorwheel_get_color(wheel));

    snprintf(cw_r_buf, sizeof(cw_r_buf), "R %3u", (unsigned)r);
    snprintf(cw_g_buf, sizeof(cw_g_buf), "G %3u", (unsigned)g);
    snprintf(cw_b_buf, sizeof(cw_b_buf), "B %3u", (unsigned)b);
    we_label_set_text(&cw_lbl_r, cw_r_buf);
    we_label_set_text(&cw_lbl_g, cw_g_buf);
    we_label_set_text(&cw_lbl_b, cw_b_buf);
}

/**
 * @brief 色轮选色变化回调
 * @param cw 色轮控件指针（回调透传）
 * @param c 当前选中颜色
 * @return 无
 */
static void _cw_demo_changed_cb(void *cw, colour_t c)
{
    (void)c;
    _cw_demo_sync((we_colorwheel_obj_t *)cw);
}

/**
 * @brief 初始化 colorwheel preview demo 场景
 * @param lcd GUI 运行时上下文
 * @return 无
 */
void we_colorwheel_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    cw_fps_timer   = 0U;
    cw_last_frames = 0U;
    memset(cw_fps_buf, 0, sizeof(cw_fps_buf));
    memset(cw_r_buf, 0, sizeof(cw_r_buf));
    memset(cw_g_buf, 0, sizeof(cw_g_buf));
    memset(cw_b_buf, 0, sizeof(cw_b_buf));

    we_label_obj_init(&cw_title, lcd, 10, 10,
                      "COLORWHEEL PREVIEW", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&cw_note, lcd, 10, 32,
                      "drag ring to pick hue", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&cw_fps, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 左侧色轮：156×156，起始 hue = 红色 */
    we_colorwheel_obj_init(&cw_wheel, lcd, 14, 66, 156U);
    we_colorwheel_set_changed_cb(&cw_wheel, _cw_demo_changed_cb);

    /* 右侧选中色大色块（圆角 + 细边框） */
    we_box_obj_init(&cw_swatch, lcd, 186, 70, 80, 58);
    we_box_set_radius(&cw_swatch, 12U);
    we_box_set_border(&cw_swatch, RGB888TODEV(200, 210, 224), 2U);

    /* R/G/B 数值标签（静态缓冲，整数格式化） */
    we_label_obj_init(&cw_lbl_r, lcd, 192, 142, "R", we_font_consolas_18,
                      RGB888TODEV(255, 128, 118), 255);
    we_label_obj_init(&cw_lbl_g, lcd, 192, 164, "G", we_font_consolas_18,
                      RGB888TODEV(126, 231, 150), 255);
    we_label_obj_init(&cw_lbl_b, lcd, 192, 186, "B", we_font_consolas_18,
                      RGB888TODEV(120, 170, 255), 255);

    _cw_demo_sync(&cw_wheel); /* 初值同步一次（程序化 set 不触发回调） */
}

/**
 * @brief colorwheel preview demo 周期更新
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 * @return 无
 */
void we_colorwheel_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &cw_fps, &cw_fps_timer,
                       &cw_last_frames, cw_fps_buf, ms_tick);
}
