/**
 * @file  demo_gauge.c
 * @brief 仪表盘（gauge）控件功能 demo —— DEMO_ID 24
 *
 * 一块 150x150 大表盘居中：每 GG_PERIOD 毫秒向一个伪随机目标发起
 * we_gauge_anim_value 平滑扫动（中央动画节点推进，缓入缓出），
 * 表盘开口处的 label 实时联动显示当前显示值（扫动中为插值中间量）。
 */

#include "simple_widget_demos.h"
#include "demo_common.h"
#include "widgets/gauge/we_widget_gauge.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

static we_gauge_obj_t gg_gauge;
static we_label_obj_t gg_title;
static we_label_obj_t gg_fps_label;
static we_label_obj_t gg_value_label;

static uint32_t gg_fps_timer;
static uint32_t gg_last_frames;
static char     gg_fps_buf[16];
static char     gg_val_buf[12];
static uint32_t gg_acc_ms;      /* 目标切换计时器 */
static uint32_t gg_seed;        /* LCG 伪随机种子 */
static int32_t  gg_last_disp;   /* 上次显示值缓存，变化才刷 label */

/* 目标切换周期 / 单次扫动时长（毫秒） */
#define GG_PERIOD  1500U
#define GG_SWEEP_MS 900U

/* 表盘布局（280x240 基准）：150x150 居中，开口朝下处放数值 label */
#define GG_SIZE 150
#define GG_X    ((280 - GG_SIZE) / 2)
#define GG_Y    30

/**
 * @brief 取下一个伪随机扫动目标（0~100，保证与当前目标不同）。
 * @return 目标值。
 */
static int32_t _gg_next_target(void)
{
    int32_t target;

    gg_seed = gg_seed * 1103515245UL + 12345UL;
    target  = (int32_t)((gg_seed >> 16) % 101UL);
    if (target == we_gauge_get_value(&gg_gauge))
        target = (target + 41) % 101; /* 撞上当前值就错开，保证画面有动作 */
    return target;
}

/**
 * @brief 初始化 gauge preview demo。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @return 无。
 */
void we_gauge_simple_demo_init(we_lcd_t *lcd)
{
    gg_fps_timer   = 0U;
    gg_last_frames = 0U;
    gg_acc_ms      = 0U;
    gg_seed        = 20260720UL;
    gg_last_disp   = -1;
    memset(gg_fps_buf, 0, sizeof(gg_fps_buf));
    memset(gg_val_buf, 0, sizeof(gg_val_buf));

    we_label_obj_init(&gg_title, lcd, 10, 8,
                      "GAUGE preview", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&gg_fps_label, lcd,
                      we_demo_fps_x(lcd, "FPS", we_font_consolas_18), 8,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 大表盘：量程 0~100，初值 0，默认 135°起扫 270°、11 条刻度 */
    we_gauge_obj_init(&gg_gauge, lcd, GG_X, GG_Y, GG_SIZE, GG_SIZE);
    we_gauge_set_range(&gg_gauge, 0, 100);
    we_gauge_set_colors(&gg_gauge,
                        RGB888TODEV(150, 168, 196),  /* 刻度：灰蓝 */
                        RGB888TODEV(255, 106, 90));  /* 指针：亮红 */

    /* 数值 label 放在表盘开口（下方缺口）内，后创建压在表盘上层 */
    we_label_obj_init(&gg_value_label, lcd, 126, GG_Y + 120,
                      "  0", we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    /* 先来一次开场扫动，画面立即有动作 */
    we_gauge_anim_value(&gg_gauge, _gg_next_target(), GG_SWEEP_MS);
}

/**
 * @brief gauge preview demo 周期更新。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param ms_tick 传入：本轮累计毫秒数。
 * @return 无。
 */
void we_gauge_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    int32_t disp;

    if (lcd == NULL || ms_tick == 0U)
        return;

    /* 周期性向新的伪随机目标发起平滑扫动 */
    gg_acc_ms += ms_tick;
    if (gg_acc_ms >= GG_PERIOD)
    {
        gg_acc_ms = 0U;
        we_gauge_anim_value(&gg_gauge, _gg_next_target(), GG_SWEEP_MS);
    }

    /* 数值 label 跟随显示值（扫动中间量），变化才刷新文本 */
    disp = we_gauge_get_disp_value(&gg_gauge);
    if (disp != gg_last_disp)
    {
        gg_last_disp = disp;
        sprintf(gg_val_buf, "%3ld", (long)disp);
        we_label_set_text(&gg_value_label, gg_val_buf);
    }

    we_demo_update_fps(lcd, &gg_fps_label, &gg_fps_timer,
                       &gg_last_frames, gg_fps_buf, ms_tick);
}
