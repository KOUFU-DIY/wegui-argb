/**
 * @file  demo_scale.c
 * @brief 刻度尺（scale）preview demo —— DEMO_ID 110
 *
 * 一根水平尺（0~100，主步 20，小分 4）+ 一根垂直尺（-20~60，主步 20，小分 3）。
 * 两根尺的指针每 SCD_PERIOD 毫秒经 we_scale_anim_value 向各自目标表的
 * 下一个目标平滑摆动（中央动画节点推进，缓入缓出），顶部 label 实时
 * 显示当前一对目标值。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/scale/we_widget_scale.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

static we_scale_obj_t scd_h_scale;      /* 水平尺 0~100 */
static we_scale_obj_t scd_v_scale;      /* 垂直尺 -20~60 */
static we_label_obj_t scd_title;
static we_label_obj_t scd_fps_label;
static we_label_obj_t scd_target_label; /* 顶部当前目标值显示 */

static uint32_t scd_fps_timer;
static uint32_t scd_last_frames;
static char     scd_fps_buf[16];
static char     scd_target_buf[28];
static uint32_t scd_acc_ms;             /* 目标切换计时器 */
static uint8_t  scd_idx;                /* 当前目标表下标 */

/* 目标切换周期 / 单次滑动时长（毫秒） */
#define SCD_PERIOD  2000U
#define SCD_ANIM_MS 1200U

/* 两根尺各自的目标循环表（长度一致，同步换目标） */
static const int32_t scd_h_targets[] = { 0, 100, 35, 80, 15 };
static const int32_t scd_v_targets[] = { -20, 60, 0, 45, -5 };
#define SCD_TARGET_CNT (sizeof(scd_h_targets) / sizeof(scd_h_targets[0]))

/* 布局（280x240 基准）：
 * 水平尺 240px 通栏（厚 36 = 4+2+10+2+18），垂直尺 132px 水平居中（宽 52） */
#define SCD_H_X   20
#define SCD_H_Y   56
#define SCD_H_LEN 240U
#define SCD_V_X   ((280 - WE_SCALE_V_THICKNESS) / 2)
#define SCD_V_Y   100
#define SCD_V_LEN 132U

/**
 * @brief 刷新顶部目标值 label 文本。
 * @return 无。
 */
static void _scd_update_target_label(void)
{
    sprintf(scd_target_buf, "target H:%ld V:%ld",
            (long)scd_h_targets[scd_idx], (long)scd_v_targets[scd_idx]);
    we_label_set_text(&scd_target_label, scd_target_buf);
}

/**
 * @brief 初始化 scale preview demo。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @return 无。
 */
void we_scale_preview_demo_init(we_lcd_t *lcd)
{
    scd_fps_timer   = 0U;
    scd_last_frames = 0U;
    scd_acc_ms      = 0U;
    scd_idx         = 0U;
    memset(scd_fps_buf, 0, sizeof(scd_fps_buf));
    memset(scd_target_buf, 0, sizeof(scd_target_buf));

    we_label_obj_init(&scd_title, lcd, 10, 8,
                      "SCALE preview", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&scd_fps_label, lcd,
                      we_demo_fps_x(lcd, "FPS", we_font_consolas_18), 8,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 水平尺：0~100，主步 20，小分 4（题面规格） */
    we_scale_obj_init(&scd_h_scale, lcd, SCD_H_X, SCD_H_Y, SCD_H_LEN, WE_SCALE_H, we_font_consolas_18);
    we_scale_set_range(&scd_h_scale, 0, 100);
    we_scale_set_ticks(&scd_h_scale, 20U, 4U);
    we_scale_set_colors(&scd_h_scale,
                        RGB888TODEV(150, 168, 196),   /* 线：灰蓝 */
                        RGB888TODEV(196, 205, 220),   /* 数字：浅灰 */
                        RGB888TODEV(255, 106, 90));   /* 指针：亮红 */

    /* 垂直尺：-20~60（负数量程演示），主步 20，小分 3 */
    we_scale_obj_init(&scd_v_scale, lcd, SCD_V_X, SCD_V_Y, SCD_V_LEN, WE_SCALE_V, we_font_consolas_18);
    we_scale_set_range(&scd_v_scale, -20, 60);
    we_scale_set_ticks(&scd_v_scale, 20U, 3U);
    we_scale_set_colors(&scd_v_scale,
                        RGB888TODEV(150, 168, 196),
                        RGB888TODEV(196, 205, 220),
                        RGB888TODEV(120, 230, 205));  /* 指针：青绿，与水平尺区分 */

    /* 顶部目标值 label（title 下一行） */
    we_label_obj_init(&scd_target_label, lcd, 10, 30,
                      "target", we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);
    _scd_update_target_label();

    /* 开场立即向第一组目标滑动，画面立刻有动作 */
    we_scale_anim_value(&scd_h_scale, scd_h_targets[scd_idx], SCD_ANIM_MS);
    we_scale_anim_value(&scd_v_scale, scd_v_targets[scd_idx], SCD_ANIM_MS);
}

/**
 * @brief scale preview demo 周期更新。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param ms_tick 传入：本轮累计毫秒数。
 * @return 无。
 */
void we_scale_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    /* 每 SCD_PERIOD 换下一组目标并发起平滑摆动 */
    scd_acc_ms += ms_tick;
    if (scd_acc_ms >= SCD_PERIOD)
    {
        scd_acc_ms = 0U;
        scd_idx = (uint8_t)((scd_idx + 1U) % SCD_TARGET_CNT);
        we_scale_anim_value(&scd_h_scale, scd_h_targets[scd_idx], SCD_ANIM_MS);
        we_scale_anim_value(&scd_v_scale, scd_v_targets[scd_idx], SCD_ANIM_MS);
        _scd_update_target_label();
    }

    we_demo_update_fps(lcd, &scd_fps_label, &scd_fps_timer,
                       &scd_last_frames, scd_fps_buf, ms_tick);
}
