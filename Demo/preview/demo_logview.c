/**
 * @file  demo_logview.c
 * @brief 滚动日志窗（logview）preview demo —— 模拟日志流（DEMO_ID 105）
 *
 * 一块 240x150 日志窗占屏幕中部：tick 每 400ms 从循环模板数组取一条 +
 * 递增序号 sprintf 成模拟日志 push 进窗（16 行 x 28 字节环形行缓冲，
 * demo 静态数组提供），演示自动滚底；向上拖拽查看历史时自动暂停跟随，
 * 拖回最底恢复。顶部说明 label 随跟随状态联动提示。FPS 照常显示。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/logview/we_widget_logview.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

/* 行缓冲容量：16 行 x 28 字节（调用方持有，控件环形复用） */
#define LG_LINE_LEN 28U
#define LG_LINE_CNT 16U

/* 模拟日志产出周期（毫秒） */
#define LG_PUSH_MS 400U

/* 布局（280x240 基准） */
#define LG_PANEL_X 20
#define LG_PANEL_Y 64
#define LG_PANEL_W 240
#define LG_PANEL_H 150

static we_label_obj_t lg_title;
static we_label_obj_t lg_fps_label;
static we_label_obj_t lg_hint_label;   /* 顶部跟随状态提示 */
static we_logview_obj_t lg_view;

static uint32_t lg_fps_timer;
static uint32_t lg_last_frames;
static char lg_fps_buf[16];

static uint32_t lg_acc_ms;             /* push 节拍累积器 */
static uint16_t lg_seq;                /* 日志递增序号 */
static uint8_t lg_last_follow;         /* 上次显示的跟随状态 */
static char lg_msg_buf[32];            /* 单条日志组装缓冲 */
static char lg_line_buf[LG_LINE_CNT * LG_LINE_LEN]; /* 控件行存储（扁平二维） */

/* 循环日志模板（配合递增序号模拟真实日志流） */
static const char *const lg_tpl[8] = {
    "sys boot ok",
    "sensor T=23.5C",
    "wifi rssi -52dBm",
    "batt 3.98V chg",
    "gpio irq PB4",
    "flash write ok",
    "uart rx 24B",
    "task wd feed",
};

/**
 * @brief 初始化 logview demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_logview_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    lg_fps_timer = 0U;
    lg_last_frames = 0U;
    lg_acc_ms = 0U;
    lg_seq = 0U;
    lg_last_follow = 1U;
    memset(lg_fps_buf, 0, sizeof(lg_fps_buf));
    memset(lg_msg_buf, 0, sizeof(lg_msg_buf));
    memset(lg_line_buf, 0, sizeof(lg_line_buf));

    we_label_obj_init(&lg_title, lcd, 14, 10,
                      "LOGVIEW", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&lg_fps_label, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* 顶部说明：提示拖拽可暂停自动跟随 */
    we_label_obj_init(&lg_hint_label, lcd, 14, 36,
                      "drag to pause follow", we_font_consolas_18,
                      RGB888TODEV(112, 184, 255), 255);

    /* 日志窗占中部：行存储由 demo 静态数组提供，环形复用 */
    we_logview_obj_init(&lg_view, lcd, LG_PANEL_X, LG_PANEL_Y,
                        LG_PANEL_W, LG_PANEL_H,
                        lg_line_buf, LG_LINE_LEN, LG_LINE_CNT, we_font_consolas_18);
}

/**
 * @brief logview demo 周期更新：每 400ms push 一条模拟日志 + 跟随状态提示
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_logview_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint8_t follow;

    if (lcd == NULL || ms_tick == 0U)
        return;

    /* 1. 周期产出模拟日志：循环模板 + 递增序号 */
    lg_acc_ms += ms_tick;
    while (lg_acc_ms >= LG_PUSH_MS)
    {
        lg_acc_ms -= LG_PUSH_MS;
        sprintf(lg_msg_buf, "[%03u] %s",
                (unsigned)(lg_seq % 1000U), lg_tpl[lg_seq % 8U]);
        lg_seq++;
        we_logview_push(&lg_view, lg_msg_buf);
    }

    /* 2. 跟随状态变化时联动顶部提示（拖离底部暂停 / 拖回恢复） */
    follow = we_logview_get_follow(&lg_view);
    if (follow != lg_last_follow)
    {
        lg_last_follow = follow;
        if (follow)
        {
            we_label_set_color(&lg_hint_label, RGB888TODEV(112, 184, 255));
            we_label_set_text(&lg_hint_label, "drag to pause follow");
        }
        else
        {
            we_label_set_color(&lg_hint_label, RGB888TODEV(255, 176, 60));
            we_label_set_text(&lg_hint_label, "paused: drag to bottom");
        }
    }

    we_demo_update_fps(lcd, &lg_fps_label, &lg_fps_timer,
                       &lg_last_frames, lg_fps_buf, ms_tick);
}
