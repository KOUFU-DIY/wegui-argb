/**
 * @file  demo_knob.c
 * @brief 弧形旋钮（knob）preview demo —— DEMO_ID 118
 *
 * 左侧 150px 大旋钮：沿弧拖动（或点击弧上任意位置）改值（0~100）；
 * 右侧用 label_ex 以 2 倍字号实时联动显示百分比，
 * 数值更新完全由 we_knob_set_changed_cb 回调驱动（无轮询）。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/knob/we_widget_knob.h"
#include "widgets/label/we_widget_label.h"
#include "widgets/label_ex/we_widget_label_ex.h"
#include <stdio.h>
#include <string.h>

static we_knob_obj_t     kb_knob;
static we_label_obj_t    kb_title;
static we_label_obj_t    kb_hint;
static we_label_obj_t    kb_fps_label;
static we_label_ex_obj_t kb_pct_label;   /* 2 倍字号大百分比 */

static uint32_t kb_fps_timer;
static uint32_t kb_last_frames;
static char     kb_fps_buf[16];
static char     kb_pct_buf[12];

/* 旋钮布局（280x240 基准）与初始值 */
#define KB_SIZE   150
#define KB_X      14
#define KB_Y      62
#define KB_INIT_V 35

/* 百分比 label_ex 的变换中心与缩放（256 = 1.0x）；
 * 中心取 214：三位数 "100%" 放大 2 倍后左沿仍不压到旋钮弧带 */
#define KB_PCT_CX    214
#define KB_PCT_CY    130
#define KB_PCT_SCALE 512U

/**
 * @brief 刷新大百分比文本。
 * @param v 当前值（0~100）。
 * @return 无。
 */
static void _kb_update_pct(int32_t v)
{
    sprintf(kb_pct_buf, "%ld%%", (long)v);
    we_label_ex_set_text(&kb_pct_label, kb_pct_buf);
}

/**
 * @brief knob 数值变化回调：大百分比 label 实时联动。
 * @param obj 触发回调的 knob 对象指针（未使用）。
 * @param value 变化后的新值。
 * @return 无。
 */
static void _kb_on_changed(void *obj, int32_t value)
{
    (void)obj;
    _kb_update_pct(value);
}

/**
 * @brief 初始化 knob preview demo。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @return 无。
 */
void we_knob_preview_demo_init(we_lcd_t *lcd)
{
    kb_fps_timer   = 0U;
    kb_last_frames = 0U;
    memset(kb_fps_buf, 0, sizeof(kb_fps_buf));
    memset(kb_pct_buf, 0, sizeof(kb_pct_buf));

    we_label_obj_init(&kb_title, lcd, 10, 8,
                      "KNOB preview", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&kb_hint, lcd, 10, 30,
                      "drag arc / tap to set", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&kb_fps_label, lcd,
                      we_demo_fps_x(lcd, "FPS", we_font_consolas_18), 8,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 大旋钮：量程 0~100，初值 KB_INIT_V（程序设值，不触发回调） */
    we_knob_obj_init(&kb_knob, lcd, KB_X, KB_Y, KB_SIZE);
    we_knob_set_range(&kb_knob, 0, 100);
    we_knob_set_value(&kb_knob, KB_INIT_V);
    we_knob_set_changed_cb(&kb_knob, _kb_on_changed);

    /* 大百分比：label_ex 2 倍缩放，围绕中心自动居中 */
    sprintf(kb_pct_buf, "%d%%", KB_INIT_V);
    we_label_ex_obj_init(&kb_pct_label, lcd, KB_PCT_CX, KB_PCT_CY,
                         kb_pct_buf, we_font_consolas_18,
                         RGB888TODEV(245, 214, 120), 255);
    we_label_ex_set_transform(&kb_pct_label, 0, (uint16_t)KB_PCT_SCALE);
}

/**
 * @brief knob preview demo 周期更新。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param ms_tick 传入：本轮累计毫秒数。
 * @return 无。
 */
void we_knob_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    /* 数值联动由 changed_cb 驱动，tick 内只维护 FPS 显示 */
    we_demo_update_fps(lcd, &kb_fps_label, &kb_fps_timer,
                       &kb_last_frames, kb_fps_buf, ms_tick);
}
