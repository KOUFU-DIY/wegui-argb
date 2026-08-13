/**
 * @file  demo_imgbtn.c
 * @brief 图片按钮（imgbtn）控件功能 demo —— DEMO_ID 32
 *
 * 演示内容：
 * 1. 左：RGB565 未压缩图按钮，按压叠半透明黑变暗（不透明图路径）
 * 2. 中：A8 透明位图图标按钮，用前景色上色，按压走整体透明度变暗
 *    （透明区不会出现方形黑影）；每次点击换一种前景色
 * 3. 右：运行时换图（we_imgbtn_set_imgs），点击在两个图标间切换，
 *    模拟播放/暂停这类切图按钮
 * 4. 点击分别累加 L / M / R 计数，下方 label 实时显示
 * 5. 右上角 FPS
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "res_img.h"
#include "widgets/imgbtn/we_widget_imgbtn.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t  ib_title;
static we_label_obj_t  ib_note;
static we_label_obj_t  ib_fps;
static we_label_obj_t  ib_count_lbl;
static we_imgbtn_obj_t ib_btn_rgb;   /* RGB565 未压缩图 */
static we_imgbtn_obj_t ib_btn_tint;  /* A8 透明位图 + 前景色上色 */
static we_imgbtn_obj_t ib_btn_swap;  /* 运行时换图 */

static uint32_t ib_fps_timer;
static uint32_t ib_last_frames;
static char     ib_fps_buf[16];

static uint16_t ib_click_l;
static uint16_t ib_click_m;
static uint16_t ib_click_r;
static uint8_t  ib_tint_idx;   /* 中间按钮当前前景色序号 */
static uint8_t  ib_swap_state; /* 右按钮当前显示的图标 0/1 */
static char     ib_count_buf[24];

/* 中间按钮循环使用的前景色 */
static const uint8_t ib_tint_pal[4][3] = {
    { 120, 230, 205 }, { 255, 154, 102 }, { 102, 178, 255 }, { 245, 214, 120 }
};

/**
 * @brief 刷新点击计数 label
 * @return 无
 */
static void _ib_demo_update_count(void)
{
    snprintf(ib_count_buf, sizeof(ib_count_buf), "L%02u  M%02u  R%02u",
             (unsigned)ib_click_l, (unsigned)ib_click_m, (unsigned)ib_click_r);
    we_label_set_text(&ib_count_lbl, ib_count_buf);
}

/**
 * @brief 左按钮点击回调：RGB565 图按钮
 * @param btn 触发回调的按钮指针（未使用）
 * @return 无
 */
static void _ib_demo_click_rgb(we_imgbtn_obj_t *btn)
{
    (void)btn;
    ib_click_l = (uint16_t)((ib_click_l + 1U) % 100U);
    _ib_demo_update_count();
}

/**
 * @brief 中按钮点击回调：换一种前景色给 A8 位图上色
 * @param btn 触发回调的按钮指针
 * @return 无
 */
static void _ib_demo_click_tint(we_imgbtn_obj_t *btn)
{
    ib_click_m = (uint16_t)((ib_click_m + 1U) % 100U);
    ib_tint_idx = (uint8_t)((ib_tint_idx + 1U) & 0x03U);
    we_imgbtn_set_color(btn, RGB888TODEV(ib_tint_pal[ib_tint_idx][0],
                                         ib_tint_pal[ib_tint_idx][1],
                                         ib_tint_pal[ib_tint_idx][2]));
    _ib_demo_update_count();
}

/**
 * @brief 右按钮点击回调：运行时换图（播放/暂停式切图）
 * @param btn 触发回调的按钮指针
 * @return 无
 */
static void _ib_demo_click_swap(we_imgbtn_obj_t *btn)
{
    ib_click_r = (uint16_t)((ib_click_r + 1U) % 100U);
    ib_swap_state ^= 1U;
    we_imgbtn_set_imgs(btn,
                       ib_swap_state ? demo_picture_a8_indexqoimask_be_48x48
                                     : demo_mapin_a8_indexqoimask_be_48x48,
                       NULL);
    _ib_demo_update_count();
}

/**
 * @brief 初始化 imgbtn demo 场景
 * @param lcd GUI 运行时上下文
 * @return 无
 */
void we_imgbtn_simple_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    ib_fps_timer   = 0U;
    ib_last_frames = 0U;
    ib_click_l     = 0U;
    ib_click_m     = 0U;
    ib_click_r     = 0U;
    ib_tint_idx    = 0U;
    ib_swap_state  = 0U;
    memset(ib_fps_buf, 0, sizeof(ib_fps_buf));
    memset(ib_count_buf, 0, sizeof(ib_count_buf));

    we_label_obj_init(&ib_title, lcd, 10, 10,
                      "IMGBTN", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&ib_note, lcd, 10, 32,
                      "rgb / tint / swap", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&ib_fps, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 左：RGB565 未压缩图，按压叠黑变暗；点击回调直接在 init 末参传入 */
    we_imgbtn_obj_init(&ib_btn_rgb, lcd, 20, 70, demo_rgb565_raw_be_64x80, NULL,
                       _ib_demo_click_rgb);

    /* 中：A8 透明位图，前景色上色（默认白，点击换色），按压走透明度变暗 */
    we_imgbtn_obj_init(&ib_btn_tint, lcd, 116, 86, demo_windows_a8_raw_be_48x48, NULL,
                       _ib_demo_click_tint);
    we_imgbtn_set_color(&ib_btn_tint, RGB888TODEV(ib_tint_pal[0][0],
                                                  ib_tint_pal[0][1],
                                                  ib_tint_pal[0][2]));

    /* 右：点击在两个 索引QOI_MASK 压缩 A8 图标间换图，演示 set_imgs + 压缩解码 */
    we_imgbtn_obj_init(&ib_btn_swap, lcd, 200, 86, demo_mapin_a8_indexqoimask_be_48x48, NULL,
                       _ib_demo_click_swap);
    we_imgbtn_set_color(&ib_btn_swap, RGB888TODEV(255, 154, 102));

    we_label_obj_init(&ib_count_lbl, lcd, 60, 182,
                      "L00  M00  R00", we_font_consolas_18,
                      RGB888TODEV(255, 154, 102), 255);
    _ib_demo_update_count();
}

/**
 * @brief imgbtn demo 周期更新
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 * @return 无
 */
void we_imgbtn_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &ib_fps, &ib_fps_timer,
                       &ib_last_frames, ib_fps_buf, ms_tick);
}
