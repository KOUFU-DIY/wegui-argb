/**
 * @file  demo_spectrum.c
 * @brief 频谱柱（spectrum）preview demo —— 24 柱假音频包络（DEMO_ID 104）
 *
 * 一块 240x140 频谱面板承载 24 根电平柱：tick 里用 LCG 伪随机 + we_sin
 * 合成假音频包络（慢波 + 快波 + 噪声，含削顶让峰值帽频繁触顶），
 * 每 50ms push 一帧，演示快上冲/慢回落与峰值帽缓落；
 * 下方说明 label 提示 peak hold on。FPS 照常显示。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/spectrum/we_widget_spectrum.h"
#include "widgets/label/we_widget_label.h"
#include <string.h>

/* 柱数（<= WE_SPECTRUM_BAR_MAX） */
#define SP_BARS 24U

/* 假音频帧周期（毫秒） */
#define SP_FRAME_MS 50U

/* 布局（280x240 基准） */
#define SP_PANEL_X 20
#define SP_PANEL_Y 48
#define SP_PANEL_W 240
#define SP_PANEL_H 140

static we_label_obj_t sp_title;
static we_label_obj_t sp_fps_label;
static we_label_obj_t sp_hint_label;   /* 底部 toggle 风格说明 */
static we_spectrum_obj_t sp_spectrum;

static uint32_t sp_fps_timer;
static uint32_t sp_last_frames;
static char sp_fps_buf[16];

static uint32_t sp_acc_ms;             /* 帧节拍累积器 */
static uint32_t sp_lcg = 0x12345678UL; /* LCG 伪随机状态 */
static uint16_t sp_phase;              /* 包络扫描相位（512 步制） */
static uint8_t sp_levels[SP_BARS];     /* 本帧合成电平 */

/**
 * @brief 合成一帧假音频包络：慢波 + 快波 + LCG 噪声，含削顶。
 * @param out 传出：SP_BARS 个电平（0~255）
 * @return 无
 */
static void sp_make_frame(uint8_t *out)
{
    uint8_t i;

    sp_phase = (uint16_t)((sp_phase + 9U) & 0x1FFU); /* 包络整体缓慢流动 */

    for (i = 0U; i < SP_BARS; i++)
    {
        int16_t a1 = (int16_t)((sp_phase + (uint16_t)i * 21U) & 0x1FFU);
        int16_t a2 = (int16_t)((sp_phase * 3U + (uint16_t)i * 47U) & 0x1FFU);
        uint16_t s1 = (uint16_t)((we_sin(a1) + 32768) >> 8); /* 0~255 慢波 */
        uint16_t s2 = (uint16_t)((we_sin(a2) + 32768) >> 9); /* 0~127 快波 */
        uint16_t n;
        uint32_t v;

        sp_lcg = sp_lcg * 1664525UL + 1013904223UL;
        n = (uint16_t)((sp_lcg >> 24) & 0x3FU); /* 0~63 噪声 */

        /* 合成后 /2 有意超量程，clamp 削顶让峰值帽频繁触顶 */
        v = ((uint32_t)s1 * 2U + s2 + n) >> 1;
        if (v > 255U)
            v = 255U;
        out[i] = (uint8_t)v;
    }
}

/**
 * @brief 初始化 spectrum demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_spectrum_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    sp_fps_timer = 0U;
    sp_last_frames = 0U;
    sp_acc_ms = 0U;
    sp_phase = 0U;
    memset(sp_fps_buf, 0, sizeof(sp_fps_buf));
    memset(sp_levels, 0, sizeof(sp_levels));

    we_label_obj_init(&sp_title, lcd, 14, 10,
                      "SPECTRUM", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&sp_fps_label, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* 24 柱频谱面板：默认青蓝->品红渐变 + 近白峰值帽（默认开启） */
    we_spectrum_obj_init(&sp_spectrum, lcd, SP_PANEL_X, SP_PANEL_Y,
                         SP_PANEL_W, SP_PANEL_H, SP_BARS);
    we_spectrum_set_peak_hold(&sp_spectrum, 1U);

    /* 底部 toggle 风格说明：提示峰值保持处于开启态 */
    we_label_obj_init(&sp_hint_label, lcd, SP_PANEL_X, 204,
                      "[x] peak hold on", we_font_consolas_18,
                      RGB888TODEV(112, 184, 255), 255);
}

/**
 * @brief spectrum demo 周期更新：每 50ms 合成并 push 一帧假音频包络
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_spectrum_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    sp_acc_ms += ms_tick;
    while (sp_acc_ms >= SP_FRAME_MS)
    {
        sp_acc_ms -= SP_FRAME_MS;
        sp_make_frame(sp_levels);
        we_spectrum_push(&sp_spectrum, sp_levels);
    }

    we_demo_update_fps(lcd, &sp_fps_label, &sp_fps_timer,
                       &sp_last_frames, sp_fps_buf, ms_tick);
}
