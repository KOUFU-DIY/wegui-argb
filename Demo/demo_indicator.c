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
static we_label_obj_t     ind_lbl[5];
static we_indicator_obj_t ind[5];

static uint32_t ind_fps_timer;
static uint32_t ind_last_frames;
static uint32_t ind_auto_timer;
static char     ind_fps_buf[16];

/* 五盏灯的标签 */
static const char *const ind_names[5] = {
    "PWR", "LINK", "NoGlow", "Fast", "Tap"
};

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
    int16_t i;

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

    for (i = 0; i < 5; i++)
    {
        int16_t row_y = (int16_t)(start_y + row_h * i);
        int16_t lbl_y;
        int8_t  yt, yb;

        we_indicator_obj_init(&ind[i], lcd, mx, row_y, d, d);

        we_get_text_bbox(we_font_consolas_18, ind_names[i], &yt, &yb);
        lbl_y = (int16_t)(row_y + d / 2) - (yt + yb) / 2;
        we_label_obj_init(&ind_lbl[i], lcd, lbl_x, lbl_y,
                          ind_names[i], we_font_consolas_18,
                          RGB888TODEV(220, 228, 238), 255);
    }

    /* 0 PWR  : 绿，默认 250ms，带光晕 */
    we_indicator_set_anim(&ind[0], 1U, 250U);

    /* 1 LINK : 蓝，慢速 600ms，带光晕 */
    we_indicator_set_colors(&ind[1], RGB888TODEV(40, 130, 255),
                            RGB888TODEV(56, 58, 66));
    we_indicator_set_anim(&ind[1], 1U, 600U);

    /* 2 NoGlow: 橙，关闭光晕，500ms */
    we_indicator_set_colors(&ind[2], RGB888TODEV(255, 150, 40),
                            RGB888TODEV(56, 58, 66));
    we_indicator_set_glow(&ind[2], 0U);
    we_indicator_set_anim(&ind[2], 1U, 500U);

    /* 3 Fast : 红，关闭动画（瞬切） */
    we_indicator_set_colors(&ind[3], RGB888TODEV(255, 70, 70),
                            RGB888TODEV(56, 58, 66));
    we_indicator_set_anim(&ind[3], 0U, 0U);

    /* 4 Tap  : 黄，可点击翻转 */
    we_indicator_set_colors(&ind[4], RGB888TODEV(245, 214, 120),
                            RGB888TODEV(56, 58, 66));
    we_indicator_set_clickable(&ind[4], 1U);
    we_indicator_set_anim(&ind[4], 1U, 300U);
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
        we_indicator_toggle(&ind[0]);
        we_indicator_toggle(&ind[1]);
        we_indicator_toggle(&ind[2]);
        we_indicator_toggle(&ind[3]);
    }

    we_demo_update_fps(lcd, &ind_fps_label, &ind_fps_timer,
                       &ind_last_frames, ind_fps_buf, ms_tick);
}



