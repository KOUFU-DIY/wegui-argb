/**
 * @file  demo_chart_bar.c
 * @brief 柱状图（chart_bar）preview demo —— push 滚动 + set_all 整帧双面板（DEMO_ID 109）
 *
 * 上面板：24 柱滚动模式，每 250ms push 一个 LCG 噪声 + we_sin 慢波合成值，
 *         最新值从右端进入、整体左移，演示环形缓冲滚动；
 * 下面板：16 柱整帧模式，每 600ms set_all 一组随机分布，演示一次性覆盖。
 * 两面板均开横向网格线，各配说明 label。FPS 照常显示。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/chart_bar/we_widget_chart_bar.h"
#include "widgets/label/we_widget_label.h"
#include <string.h>

/* 上面板（push 滚动）节拍与布局 */
#define CBD_PUSH_MS 250U
#define CBD_TOP_X 20
#define CBD_TOP_Y 36
#define CBD_TOP_W 240
#define CBD_TOP_H 74
#define CBD_TOP_BARS 24U

/* 下面板（set_all 整帧）节拍与布局 */
#define CBD_FRAME_MS 600U
#define CBD_BOT_X 20
#define CBD_BOT_Y 138
#define CBD_BOT_W 240
#define CBD_BOT_H 70
#define CBD_BOT_BARS 16U

static we_label_obj_t cbd_title;
static we_label_obj_t cbd_fps_label;
static we_label_obj_t cbd_top_hint;    /* 上面板说明 */
static we_label_obj_t cbd_bot_hint;    /* 下面板说明 */
static we_chart_bar_obj_t cbd_top;     /* push 滚动实例 */
static we_chart_bar_obj_t cbd_bot;     /* set_all 整帧实例 */

static uint32_t cbd_fps_timer;
static uint32_t cbd_last_frames;
static char cbd_fps_buf[16];

static uint32_t cbd_push_acc;              /* push 节拍累积器 */
static uint32_t cbd_frame_acc;             /* set_all 节拍累积器 */
static uint32_t cbd_lcg = 0x2F6E2B1UL;     /* LCG 伪随机状态 */
static uint16_t cbd_phase;                 /* 慢波相位（512 步制） */
static uint8_t cbd_frame[CBD_BOT_BARS];    /* set_all 整帧缓冲 */

/**
 * @brief 合成一个 push 滚动值：we_sin 慢波 + LCG 噪声（像素高度）。
 * @return 柱高（6 ~ 68 像素，落在上面板量程 h-1=73 内）。
 */
static uint8_t cbd_make_push_value(void)
{
    uint32_t s48;
    uint8_t noise;

    cbd_phase = (uint16_t)((cbd_phase + 22U) & 0x1FFU);
    /* Q15 正弦搬到 0~65535 后缩到 0~47（纯整数） */
    s48 = ((uint32_t)((int32_t)we_sin((int16_t)cbd_phase) + 32768L) * 48U) >> 16;

    cbd_lcg = cbd_lcg * 1664525UL + 1013904223UL;
    noise = (uint8_t)((cbd_lcg >> 24) & 0x0FU); /* 0~15 噪声 */

    return (uint8_t)(6U + s48 + noise); /* 6~68 */
}

/**
 * @brief 生成一帧随机分布（下面板 set_all 用）。
 * @param out 传出：CBD_BOT_BARS 个柱高（4 ~ 61 像素，量程 h-1=69 内）
 * @return 无
 */
static void cbd_make_frame(uint8_t *out)
{
    uint8_t i;

    for (i = 0U; i < CBD_BOT_BARS; i++)
    {
        cbd_lcg = cbd_lcg * 1664525UL + 1013904223UL;
        out[i] = (uint8_t)(4U + ((((cbd_lcg >> 24) & 0xFFU) * 58U) >> 8)); /* 4~61 */
    }
}

/**
 * @brief 初始化 chart_bar demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_chart_bar_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    cbd_fps_timer = 0U;
    cbd_last_frames = 0U;
    cbd_push_acc = 0U;
    cbd_frame_acc = 0U;
    cbd_phase = 0U;
    memset(cbd_fps_buf, 0, sizeof(cbd_fps_buf));
    memset(cbd_frame, 0, sizeof(cbd_frame));

    we_label_obj_init(&cbd_title, lcd, 14, 10,
                      "CHART BAR", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&cbd_fps_label, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* 上面板：24 柱 push 滚动，默认青蓝柱色，3 条横网格 */
    we_chart_bar_obj_init(&cbd_top, lcd, CBD_TOP_X, CBD_TOP_Y,
                          CBD_TOP_W, CBD_TOP_H, CBD_TOP_BARS);
    we_chart_bar_set_grid(&cbd_top, 3U);

    we_label_obj_init(&cbd_top_hint, lcd, CBD_TOP_X, (int16_t)(CBD_TOP_Y + CBD_TOP_H + 2),
                      "push scroll / 250ms", we_font_consolas_18,
                      RGB888TODEV(112, 184, 255), 255);

    /* 下面板：16 柱 set_all 整帧，橙色柱 + 2 条横网格 */
    we_chart_bar_obj_init(&cbd_bot, lcd, CBD_BOT_X, CBD_BOT_Y,
                          CBD_BOT_W, CBD_BOT_H, CBD_BOT_BARS);
    we_chart_bar_set_colors(&cbd_bot, RGB888TODEV(244, 168, 86),
                            RGB888TODEV(96, 104, 118));
    we_chart_bar_set_grid(&cbd_bot, 2U);

    we_label_obj_init(&cbd_bot_hint, lcd, CBD_BOT_X, (int16_t)(CBD_BOT_Y + CBD_BOT_H + 2),
                      "set_all frame / 600ms", we_font_consolas_18,
                      RGB888TODEV(255, 196, 130), 255);

    /* 先各喂一次，画面起始即有内容 */
    we_chart_bar_push(&cbd_top, cbd_make_push_value());
    cbd_make_frame(cbd_frame);
    we_chart_bar_set_all(&cbd_bot, cbd_frame);
}

/**
 * @brief chart_bar demo 周期更新：250ms push 滚动 + 600ms set_all 整帧
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_chart_bar_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    cbd_push_acc += ms_tick;
    while (cbd_push_acc >= CBD_PUSH_MS)
    {
        cbd_push_acc -= CBD_PUSH_MS;
        we_chart_bar_push(&cbd_top, cbd_make_push_value());
    }

    cbd_frame_acc += ms_tick;
    while (cbd_frame_acc >= CBD_FRAME_MS)
    {
        cbd_frame_acc -= CBD_FRAME_MS;
        cbd_make_frame(cbd_frame);
        we_chart_bar_set_all(&cbd_bot, cbd_frame);
    }

    we_demo_update_fps(lcd, &cbd_fps_label, &cbd_fps_timer,
                       &cbd_last_frames, cbd_fps_buf, ms_tick);
}
