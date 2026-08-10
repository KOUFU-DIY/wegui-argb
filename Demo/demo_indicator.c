/**
 * @file  demo_indicator.c
 * @brief 圆形状态指示灯（Indicator）功能 demo
 *
 * 演示内容：
 * 1. 四盏不同颜色/动画速度的指示灯，定时自动亮灭，展示过渡动画
 * 2. 第 3 盏关闭光晕，与带光晕的对比
 * 3. 第 4 盏关闭动画（瞬切），与平滑过渡对比
 * 4. 一盏可点击翻转的指示灯（clickable）
 */

#include "simple_widget_demos.h"
#include "demo_common.h"
#include "widgets/indicator/we_widget_indicator.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t     ind_title;
static we_label_obj_t     ind_fps_label;
static we_label_obj_t     ind_hint;
static we_label_obj_t     ind_pwr_lbl;
static we_label_obj_t     ind_link_lbl;
static we_label_obj_t     ind_noglow_lbl;
static we_label_obj_t     ind_fast_lbl;
static we_label_obj_t     ind_tap_lbl;
static we_indicator_obj_t ind_pwr;
static we_indicator_obj_t ind_link;
static we_indicator_obj_t ind_noglow;
static we_indicator_obj_t ind_fast;
static we_indicator_obj_t ind_tap;

static uint32_t ind_fps_timer;
static uint32_t ind_last_frames;
static uint32_t ind_auto_timer;
static char     ind_fps_buf[16];

/**
 * @brief 初始化 indicator demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_indicator_simple_demo_init(we_lcd_t *lcd)
{
    int16_t mx      = 14;
    int16_t title_y = 10;
    int16_t hint_y  = 32;
    int16_t fps_x   = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    int16_t row_h   = 36;
    int16_t start_y = 58;
    int16_t d       = 30;          /* 指示灯直径 */
    int16_t lbl_x   = (int16_t)(mx + d + 14);

    ind_fps_timer   = 0U;
    ind_last_frames = 0U;
    ind_auto_timer  = 0U;
    memset(ind_fps_buf, 0, sizeof(ind_fps_buf));

    we_label_obj_init(&ind_title, lcd, mx, title_y,
                      "INDICATOR", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&ind_fps_label, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&ind_hint, lcd, mx, hint_y,
                      "glow|speed|tap", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);

    /* 第 1 行 PWR：绿（默认色），250ms 过渡，带光晕 */
    {
        int16_t row_y = start_y;
        int16_t lbl_y;
        int8_t  yt, yb;

        we_indicator_obj_init(&ind_pwr, lcd, mx, row_y, d, d);
        we_indicator_set_anim(&ind_pwr, 1U, 250U);
        we_get_text_bbox(we_font_consolas_18, "PWR", &yt, &yb);
        lbl_y = (int16_t)(row_y + d / 2) - (yt + yb) / 2;
        we_label_obj_init(&ind_pwr_lbl, lcd, lbl_x, lbl_y,
                          "PWR", we_font_consolas_18,
                          RGB888TODEV(220, 228, 238), 255);
    }

    /* 第 2 行 LINK：蓝，慢速 600ms，带光晕 */
    {
        int16_t row_y = (int16_t)(start_y + row_h);
        int16_t lbl_y;
        int8_t  yt, yb;

        we_indicator_obj_init(&ind_link, lcd, mx, row_y, d, d);
        we_indicator_set_colors(&ind_link, RGB888TODEV(40, 130, 255),
                                RGB888TODEV(56, 58, 66));
        we_indicator_set_anim(&ind_link, 1U, 600U);
        we_get_text_bbox(we_font_consolas_18, "LINK", &yt, &yb);
        lbl_y = (int16_t)(row_y + d / 2) - (yt + yb) / 2;
        we_label_obj_init(&ind_link_lbl, lcd, lbl_x, lbl_y,
                          "LINK", we_font_consolas_18,
                          RGB888TODEV(220, 228, 238), 255);
    }

    /* 第 3 行 NoGlow：橙，关闭光晕，500ms */
    {
        int16_t row_y = (int16_t)(start_y + row_h * 2);
        int16_t lbl_y;
        int8_t  yt, yb;

        we_indicator_obj_init(&ind_noglow, lcd, mx, row_y, d, d);
        we_indicator_set_colors(&ind_noglow, RGB888TODEV(255, 150, 40),
                                RGB888TODEV(56, 58, 66));
        we_indicator_set_glow(&ind_noglow, 0U);
        we_indicator_set_anim(&ind_noglow, 1U, 500U);
        we_get_text_bbox(we_font_consolas_18, "NoGlow", &yt, &yb);
        lbl_y = (int16_t)(row_y + d / 2) - (yt + yb) / 2;
        we_label_obj_init(&ind_noglow_lbl, lcd, lbl_x, lbl_y,
                          "NoGlow", we_font_consolas_18,
                          RGB888TODEV(220, 228, 238), 255);
    }

    /* 第 4 行 Fast：红，关闭动画（瞬切） */
    {
        int16_t row_y = (int16_t)(start_y + row_h * 3);
        int16_t lbl_y;
        int8_t  yt, yb;

        we_indicator_obj_init(&ind_fast, lcd, mx, row_y, d, d);
        we_indicator_set_colors(&ind_fast, RGB888TODEV(255, 70, 70),
                                RGB888TODEV(56, 58, 66));
        we_indicator_set_anim(&ind_fast, 0U, 0U);
        we_get_text_bbox(we_font_consolas_18, "Fast", &yt, &yb);
        lbl_y = (int16_t)(row_y + d / 2) - (yt + yb) / 2;
        we_label_obj_init(&ind_fast_lbl, lcd, lbl_x, lbl_y,
                          "Fast", we_font_consolas_18,
                          RGB888TODEV(220, 228, 238), 255);
    }

    /* 第 5 行 Tap：黄，可点击翻转 */
    {
        int16_t row_y = (int16_t)(start_y + row_h * 4);
        int16_t lbl_y;
        int8_t  yt, yb;

        we_indicator_obj_init(&ind_tap, lcd, mx, row_y, d, d);
        we_indicator_set_colors(&ind_tap, RGB888TODEV(245, 214, 120),
                                RGB888TODEV(56, 58, 66));
        we_indicator_set_clickable(&ind_tap, 1U);
        we_indicator_set_anim(&ind_tap, 1U, 300U);
        we_get_text_bbox(we_font_consolas_18, "Tap", &yt, &yb);
        lbl_y = (int16_t)(row_y + d / 2) - (yt + yb) / 2;
        we_label_obj_init(&ind_tap_lbl, lcd, lbl_x, lbl_y,
                          "Tap", we_font_consolas_18,
                          RGB888TODEV(220, 228, 238), 255);
    }
}

/**
 * @brief indicator demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_indicator_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    ind_auto_timer += ms_tick;

    /* 每 1.5s 自动翻转前四盏灯（第 5 盏交由点击控制），演示亮灭过渡 */
    if (ind_auto_timer >= 1500U)
    {
        ind_auto_timer = 0U;
        we_indicator_toggle(&ind_pwr);
        we_indicator_toggle(&ind_link);
        we_indicator_toggle(&ind_noglow);
        we_indicator_toggle(&ind_fast);
    }

    we_demo_update_fps(lcd, &ind_fps_label, &ind_fps_timer,
                       &ind_last_frames, ind_fps_buf, ms_tick);
}
