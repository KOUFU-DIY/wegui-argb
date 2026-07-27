/**
 * @file  demo_statusbar.c
 * @brief 状态栏（statusbar）preview demo —— DEMO_ID 112
 *
 * 状态栏置顶（280px 全宽），下方大 label 联动回显各值。tick 驱动：
 *   - 时间每"分钟"+1（加速为每 2 秒一分钟）；
 *   - 电池 100→5 每 400ms 减 5%（过 20% 图标变低电色），到 5% 切
 *     charging 态快速回满（约 3 秒）后恢复递减循环；
 *   - WiFi 每 1.2s、信号每 0.9s 各自按序列表变格（含 -1 隐藏档），
 * 画面持续有变化，适合录 GIF。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/statusbar/we_widget_statusbar.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

/* 节奏参数（毫秒） */
#define SB_MINUTE_MS 2000U /* 加速时钟：每 2 秒 = 1 分钟 */
#define SB_DECAY_MS  400U  /* 电池递减步进周期 */
#define SB_CHARGE_MS 150U  /* 充电回升步进周期（5%→100% 约 3 秒） */
#define SB_WIFI_MS   1200U /* WiFi 变格周期 */
#define SB_SIG_MS    900U  /* 信号变格周期 */

static we_statusbar_obj_t sb_bar;
static we_label_obj_t     sb_title;
static we_label_obj_t     sb_info1;     /* 电池/充电回显 */
static we_label_obj_t     sb_info2;     /* WiFi/信号/时间回显 */
static we_label_obj_t     sb_fps_label;

static uint32_t sb_fps_timer;
static uint32_t sb_last_frames;
static char     sb_fps_buf[16];

static char sb_time_buf[8];   /* "HH:MM"（调用方持有，控件只存指针） */
static char sb_info1_buf[28];
static char sb_info2_buf[28];

static uint32_t sb_t_minute;  /* 时钟累计 */
static uint32_t sb_t_bat;     /* 电池步进累计 */
static uint32_t sb_t_wifi;    /* WiFi 变格累计 */
static uint32_t sb_t_sig;     /* 信号变格累计 */

static uint8_t sb_hour;
static uint8_t sb_minute;
static uint8_t sb_bat_pct;
static uint8_t sb_charging;   /* 0 = 递减阶段，1 = 充电回升阶段 */
static uint8_t sb_wifi_idx;
static uint8_t sb_sig_idx;

/* WiFi / 信号变格序列（含 -1 隐藏档，循环播放） */
static const int8_t sb_wifi_seq[8] = { 3, 2, 1, 0, -1, 0, 1, 2 };
static const int8_t sb_sig_seq[8]  = { 4, 3, 2, 1, 0, -1, 1, 3 };

/**
 * @brief 刷新时间缓冲并同步到状态栏。
 * @return 无。
 */
static void _sb_demo_apply_time(void)
{
    sprintf(sb_time_buf, "%02d:%02d", (int)sb_hour, (int)sb_minute);
    we_statusbar_set_time(&sb_bar, sb_time_buf);
}

/**
 * @brief 刷新下方两行回显 label（电池行 + 连接/时间行）。
 * @return 无。
 */
static void _sb_demo_refresh_info(void)
{
    char wifi_txt[6];
    char sig_txt[6];

    sprintf(sb_info1_buf, "bat %d%%%s", (int)sb_bat_pct,
            sb_charging ? "  charging" : "");
    we_label_set_text(&sb_info1, sb_info1_buf);

    /* -1 隐藏档回显为 "off"（两路可能同时为 -1，分开格式化） */
    if (sb_wifi_seq[sb_wifi_idx] < 0)
        sprintf(wifi_txt, "off");
    else
        sprintf(wifi_txt, "%d", (int)sb_wifi_seq[sb_wifi_idx]);
    if (sb_sig_seq[sb_sig_idx] < 0)
        sprintf(sig_txt, "off");
    else
        sprintf(sig_txt, "%d", (int)sb_sig_seq[sb_sig_idx]);
    sprintf(sb_info2_buf, "wifi %s  sig %s", wifi_txt, sig_txt);
    we_label_set_text(&sb_info2, sb_info2_buf);
}

/**
 * @brief 初始化 statusbar preview demo。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @return 无。
 */
void we_statusbar_preview_demo_init(we_lcd_t *lcd)
{
    sb_fps_timer   = 0U;
    sb_last_frames = 0U;
    memset(sb_fps_buf, 0, sizeof(sb_fps_buf));

    sb_t_minute = 0U;
    sb_t_bat    = 0U;
    sb_t_wifi   = 0U;
    sb_t_sig    = 0U;
    sb_hour     = 12U;
    sb_minute   = 0U;
    sb_bat_pct  = 100U;
    sb_charging = 0U;
    sb_wifi_idx = 0U;
    sb_sig_idx  = 0U;

    /* 状态栏置顶（全宽），初值：12:00 / 100% / 不充电 / WiFi 3 / 信号 4 */
    we_statusbar_obj_init(&sb_bar, lcd, 0, 0, (uint16_t)lcd->width, we_font_consolas_18);
    _sb_demo_apply_time();
    we_statusbar_set_battery(&sb_bar, sb_bat_pct);
    we_statusbar_set_wifi(&sb_bar, sb_wifi_seq[0]);
    we_statusbar_set_signal(&sb_bar, sb_sig_seq[0]);

    we_label_obj_init(&sb_title, lcd, 10, 34,
                      "STATUSBAR preview", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&sb_fps_label, lcd,
                      we_demo_fps_x(lcd, "FPS", we_font_consolas_18), 34,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 下方大 label 联动回显 */
    sprintf(sb_info1_buf, "bat 100%%");
    we_label_obj_init(&sb_info1, lcd, 10, 84,
                      sb_info1_buf, we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);
    sprintf(sb_info2_buf, "wifi 3  sig 4");
    we_label_obj_init(&sb_info2, lcd, 10, 112,
                      sb_info2_buf, we_font_consolas_18,
                      RGB888TODEV(140, 200, 255), 255);
}

/**
 * @brief statusbar preview demo 周期更新：时钟/电池/WiFi/信号四路驱动。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param ms_tick 传入：本轮累计毫秒数。
 * @return 无。
 */
void we_statusbar_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint8_t info_dirty = 0U;

    if (lcd == NULL || ms_tick == 0U)
        return;

    /* 1. 加速时钟：每 SB_MINUTE_MS 走一分钟 */
    sb_t_minute += ms_tick;
    if (sb_t_minute >= SB_MINUTE_MS)
    {
        sb_t_minute -= SB_MINUTE_MS;
        sb_minute++;
        if (sb_minute >= 60U)
        {
            sb_minute = 0U;
            sb_hour = (uint8_t)((sb_hour + 1U) % 24U);
        }
        _sb_demo_apply_time();
        info_dirty = 1U;
    }

    /* 2. 电池：递减到 5% 切充电快速回满，满后恢复递减 */
    sb_t_bat += ms_tick;
    if (!sb_charging && sb_t_bat >= SB_DECAY_MS)
    {
        sb_t_bat = 0U;
        sb_bat_pct = (uint8_t)((sb_bat_pct > 10U) ? (sb_bat_pct - 5U) : 5U);
        we_statusbar_set_battery(&sb_bar, sb_bat_pct);
        if (sb_bat_pct <= 5U)
        {
            sb_charging = 1U; /* 触底：进入充电阶段（图标叠闪电+变绿） */
            we_statusbar_set_charging(&sb_bar, 1U);
        }
        info_dirty = 1U;
    }
    else if (sb_charging && sb_t_bat >= SB_CHARGE_MS)
    {
        sb_t_bat = 0U;
        sb_bat_pct = (uint8_t)((sb_bat_pct < 95U) ? (sb_bat_pct + 5U) : 100U);
        we_statusbar_set_battery(&sb_bar, sb_bat_pct);
        if (sb_bat_pct >= 100U)
        {
            sb_charging = 0U; /* 充满：退出充电阶段，恢复递减 */
            we_statusbar_set_charging(&sb_bar, 0U);
        }
        info_dirty = 1U;
    }

    /* 3. WiFi 变格（含 -1 隐藏档） */
    sb_t_wifi += ms_tick;
    if (sb_t_wifi >= SB_WIFI_MS)
    {
        sb_t_wifi -= SB_WIFI_MS;
        sb_wifi_idx = (uint8_t)((sb_wifi_idx + 1U) % 8U);
        we_statusbar_set_wifi(&sb_bar, sb_wifi_seq[sb_wifi_idx]);
        info_dirty = 1U;
    }

    /* 4. 信号变格（与 WiFi 错周期，画面节奏更自然） */
    sb_t_sig += ms_tick;
    if (sb_t_sig >= SB_SIG_MS)
    {
        sb_t_sig -= SB_SIG_MS;
        sb_sig_idx = (uint8_t)((sb_sig_idx + 1U) % 8U);
        we_statusbar_set_signal(&sb_bar, sb_sig_seq[sb_sig_idx]);
        info_dirty = 1U;
    }

    if (info_dirty)
        _sb_demo_refresh_info();

    we_demo_update_fps(lcd, &sb_fps_label, &sb_fps_timer,
                       &sb_last_frames, sb_fps_buf, ms_tick);
}
