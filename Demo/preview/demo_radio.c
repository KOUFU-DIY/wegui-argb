/**
 * @file  demo_radio.c
 * @brief 单选组（radio，preview）demo —— Low/Mid/High 三选项 + 顶部回显
 *
 * 一组三行互斥单选（"Low"/"Mid"/"High"），点击行切换选中，
 * changed_cb 里把当前选中项名写到顶部状态 label。
 * 演示点：互斥切换、值变才回调、按压行高亮、行级标脏。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets/label/we_widget_label.h"
#include "widgets_preview/radio/we_widget_radio.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t rd_title;
static we_label_obj_t rd_fps_label;
static we_label_obj_t rd_status;
static we_radio_obj_t rd_group;

static uint32_t rd_fps_timer;
static uint32_t rd_last_frames;
static char     rd_fps_buf[16];
static char     rd_status_buf[24];

/* 选项名数组：demo 静态持有，控件只存指针 */
static const char *const rd_options[3] = { "Low", "Mid", "High" };

/**
 * @brief 选中改变回调：把当前选中项名写到顶部状态 label
 * @param radio 传入：触发回调的单选组对象指针
 * @param idx 传入：新选中的行序号
 * @return 无
 */
static void _rd_changed(void *radio, uint8_t idx)
{
    (void)radio;
    if (idx >= 3U)
        return;

    snprintf(rd_status_buf, sizeof(rd_status_buf), "SEL: %s", rd_options[idx]);
    we_label_set_text(&rd_status, rd_status_buf);
}

/**
 * @brief 初始化 radio preview demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_radio_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    rd_fps_timer   = 0U;
    rd_last_frames = 0U;
    memset(rd_fps_buf, 0, sizeof(rd_fps_buf));

    we_label_obj_init(&rd_title, lcd, 14, 8,
                      "RADIO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&rd_fps_label, lcd, fps_x, 8,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 顶部回显：初始与默认选中项（第 0 行）一致 */
    snprintf(rd_status_buf, sizeof(rd_status_buf), "SEL: %s", rd_options[0]);
    we_label_obj_init(&rd_status, lcd, 14, 34,
                      rd_status_buf, we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    /* 三行单选组：总高由控件按字体行高自动算出 */
    we_radio_obj_init(&rd_group, lcd, 24, 74, 200, rd_options, 3U, we_font_consolas_18);
    we_radio_set_changed_cb(&rd_group, _rd_changed);
}

/**
 * @brief radio preview demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_radio_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &rd_fps_label, &rd_fps_timer,
                       &rd_last_frames, rd_fps_buf, ms_tick);
}
