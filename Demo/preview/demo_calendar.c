/**
 * @file  demo_calendar.c
 * @brief 日历（calendar）preview demo —— 月视图选日 + 自动翻月轮播（DEMO_ID 106）
 *
 * 一块 196x176 月视图日历，初始 2026-07-20（选中块 + 今日环同格叠加）。
 * 顶部 SEL 标签由 changed 回调驱动：点日期格 / 点 "<" ">" 热区 / 快速
 * 横滑翻月都会即时回显新选中日期。tick 每 2.5 秒自动 next_month 一次，
 * 翻满 3 个月后 set_month 回到 2026-07 起点循环（程序设置不触发回调，
 * 标签由 tick 主动同步），画面持续有变化供录 GIF。FPS 照常显示。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/calendar/we_widget_calendar.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t cal_title;
static we_label_obj_t cal_fps_label;
static we_label_obj_t cal_sel_label;    /* 顶部选中日期回显 */
static we_calendar_obj_t cal_widget;

static uint32_t cal_fps_timer;
static uint32_t cal_last_frames;
static uint32_t cal_auto_timer;         /* 自动翻月累计计时 */
static uint8_t cal_auto_step;           /* 自动轮播步序 0..3 */
static char cal_fps_buf[16];
static char cal_sel_buf[24];

/* 布局（280x240 基准）：日历 7 列 x 8 行等分，格 28x22 */
#define CAL_X 42
#define CAL_Y 58
#define CAL_W 196
#define CAL_H 176

/* 自动翻月节奏：每 2.5 秒一步，3 步 next 后第 4 步回到起点 */
#define CAL_AUTO_PERIOD 2500U

/* 演示起点日期 */
#define CAL_START_YEAR 2026U
#define CAL_START_MONTH 7U
#define CAL_START_DAY 20U

/**
 * @brief 把日历当前选中日期格式化回显到顶部 SEL 标签。
 * @return 无
 * @note 回调路径与程序自动翻月路径共用此函数，保证标签始终同步。
 */
static void cal_show_sel(void)
{
    uint16_t y;
    uint8_t m;
    uint8_t d;

    we_calendar_get_selected(&cal_widget, &y, &m, &d);
    sprintf(cal_sel_buf, "SEL: %04u-%02u-%02u",
            (unsigned)y, (unsigned)m, (unsigned)d);
    we_label_set_text(&cal_sel_label, cal_sel_buf);
}

/**
 * @brief 选中变化回调：用户点日期格 / 翻月热区 / 横滑翻月时刷新 SEL 标签。
 * @param cal 传入：日历控件对象指针（void * 透传，本 demo 未使用）
 * @param y 传入：新选中年份
 * @param m 传入：新选中月份
 * @param d 传入：新选中日
 * @return 无
 */
static void cal_on_changed(void *cal, uint16_t y, uint8_t m, uint8_t d)
{
    (void)cal;
    (void)y;
    (void)m;
    (void)d;
    cal_show_sel();
}

/**
 * @brief 初始化 calendar demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_calendar_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    cal_fps_timer = 0U;
    cal_last_frames = 0U;
    cal_auto_timer = 0U;
    cal_auto_step = 0U;
    memset(cal_fps_buf, 0, sizeof(cal_fps_buf));
    memset(cal_sel_buf, 0, sizeof(cal_sel_buf));

    we_label_obj_init(&cal_title, lcd, 14, 10,
                      "CALENDAR", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&cal_fps_label, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* 日历：初始 2026-07 选中 20 日，同日标记今日环 */
    we_calendar_obj_init(&cal_widget, lcd, CAL_X, CAL_Y, CAL_W, CAL_H, we_font_consolas_18);
    we_calendar_set_month(&cal_widget, CAL_START_YEAR, (uint8_t)CAL_START_MONTH);
    we_calendar_set_selected(&cal_widget, (uint8_t)CAL_START_DAY);
    we_calendar_set_today(&cal_widget, CAL_START_YEAR, (uint8_t)CAL_START_MONTH,
                          (uint8_t)CAL_START_DAY);
    we_calendar_set_changed_cb(&cal_widget, cal_on_changed);

    /* 顶部回显：初始即显示 "SEL: 2026-07-20" */
    we_label_obj_init(&cal_sel_label, lcd, 14, 34,
                      "SEL:", we_font_consolas_18, RGB888TODEV(112, 184, 255), 255);
    cal_show_sel();
}

/**
 * @brief calendar demo 周期更新：自动翻月轮播 + FPS 刷新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_calendar_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    cal_auto_timer += ms_tick;
    if (cal_auto_timer >= CAL_AUTO_PERIOD)
    {
        cal_auto_timer = 0U;

        if (cal_auto_step < 3U)
        {
            /* 步 0/1/2：next_month 连翻三个月（07 → 08 → 09 → 10） */
            we_calendar_next_month(&cal_widget);
            cal_auto_step++;
        }
        else
        {
            /* 步 3：set_month 直接回到起点月并恢复选中日 */
            we_calendar_set_month(&cal_widget, CAL_START_YEAR, (uint8_t)CAL_START_MONTH);
            we_calendar_set_selected(&cal_widget, (uint8_t)CAL_START_DAY);
            cal_auto_step = 0U;
        }
        /* 程序设置不触发回调，这里主动同步 SEL 标签 */
        cal_show_sel();
    }

    we_demo_update_fps(lcd, &cal_fps_label, &cal_fps_timer,
                       &cal_last_frames, cal_fps_buf, ms_tick);
}
