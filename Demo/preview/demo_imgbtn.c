/**
 * @file  demo_imgbtn.c
 * @brief 图片按钮（imgbtn）preview demo —— DEMO_ID 120
 *
 * 演示内容：
 * 1. 两个图片按钮复用同一张内置 RGB565 未压缩图（demo_sprite），
 *    均走 img_pressed = NULL 的"按压叠黑变暗"路径
 * 2. 右侧按钮 set_opacity(150) 演示整体透明度（变暗遮罩随之等比衰减）
 * 3. 点击分别累加 L / R 计数，下方 label 实时显示
 * 4. 右上角 FPS
 */

#include "preview_demos.h"

#include "demo_common.h"
#include "res_images.h"
#include "widgets_preview/imgbtn/we_widget_imgbtn.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t  ib_title;
static we_label_obj_t  ib_note;
static we_label_obj_t  ib_fps;
static we_label_obj_t  ib_count_lbl;
static we_imgbtn_obj_t ib_btn_l;
static we_imgbtn_obj_t ib_btn_r;

static uint32_t ib_fps_timer;
static uint32_t ib_last_frames;
static char     ib_fps_buf[16];

static uint16_t ib_click_l;
static uint16_t ib_click_r;
static char     ib_count_buf[24];

/**
 * @brief 刷新点击计数 label
 * @return 无
 */
static void _ib_demo_update_count(void)
{
    snprintf(ib_count_buf, sizeof(ib_count_buf), "L %03u   R %03u",
             (unsigned)ib_click_l, (unsigned)ib_click_r);
    we_label_set_text(&ib_count_lbl, ib_count_buf);
}

/**
 * @brief 左按钮点击回调
 * @param btn 触发回调的按钮指针（未使用）
 * @return 无
 */
static void _ib_demo_click_l(void *btn)
{
    (void)btn;
    ib_click_l = (uint16_t)((ib_click_l + 1U) % 1000U);
    _ib_demo_update_count();
}

/**
 * @brief 右按钮点击回调
 * @param btn 触发回调的按钮指针（未使用）
 * @return 无
 */
static void _ib_demo_click_r(void *btn)
{
    (void)btn;
    ib_click_r = (uint16_t)((ib_click_r + 1U) % 1000U);
    _ib_demo_update_count();
}

/**
 * @brief 初始化 imgbtn preview demo 场景
 * @param lcd GUI 运行时上下文
 * @return 无
 */
void we_imgbtn_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    ib_fps_timer   = 0U;
    ib_last_frames = 0U;
    ib_click_l     = 0U;
    ib_click_r     = 0U;
    memset(ib_fps_buf, 0, sizeof(ib_fps_buf));
    memset(ib_count_buf, 0, sizeof(ib_count_buf));

    we_label_obj_init(&ib_title, lcd, 10, 10,
                      "IMGBTN PREVIEW", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&ib_note, lcd, 10, 32,
                      "click imgs (R op 150)", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&ib_fps, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 两个按钮共用同一张 64x80 内置图，均走按压叠黑变暗路径 */
    we_imgbtn_obj_init(&ib_btn_l, lcd, 56, 78, demo_sprite, NULL);
    we_imgbtn_set_clicked_cb(&ib_btn_l, _ib_demo_click_l);

    we_imgbtn_obj_init(&ib_btn_r, lcd, 160, 78, demo_sprite, NULL);
    we_imgbtn_set_clicked_cb(&ib_btn_r, _ib_demo_click_r);
    we_imgbtn_set_opacity(&ib_btn_r, 150U); /* 演示整体透明度 */

    we_label_obj_init(&ib_count_lbl, lcd, 74, 178,
                      "L 000   R 000", we_font_consolas_18,
                      RGB888TODEV(255, 154, 102), 255);
    _ib_demo_update_count();
}

/**
 * @brief imgbtn preview demo 周期更新
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 * @return 无
 */
void we_imgbtn_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &ib_fps, &ib_fps_timer,
                       &ib_last_frames, ib_fps_buf, ms_tick);
}
