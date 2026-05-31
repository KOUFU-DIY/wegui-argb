/**
 * @file  demo_stepper.c
 * @brief 数值步进控件（Stepper）功能 demo
 *
 * 演示内容：
 * 1. 温度步进：16.0~30.0，步进 0.5，1 位小数（定点 decimals=1）
 * 2. 音量步进：0~100，步进 5，纯整数
 * 3. 角度步进：0~359，步进 15，回绕（wrap）演示
 * 4. 按住 +/- 触发连续步进（复用 STAY 事件，不占 timer）
 * 5. 数值改变时刷新底部状态行
 */

#include "simple_widget_demos.h"
#include "demo_common.h"
#include "widgets/stepper/we_widget_stepper.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t   sp_title;
static we_label_obj_t   sp_fps_label;
static we_label_obj_t   sp_temp_cap;
static we_label_obj_t   sp_vol_cap;
static we_label_obj_t   sp_ang_cap;
static we_label_obj_t   sp_status_label;
static we_stepper_obj_t sp_temp;
static we_stepper_obj_t sp_vol;
static we_stepper_obj_t sp_ang;

static uint32_t sp_fps_timer;
static uint32_t sp_last_frames;
static char     sp_fps_buf[16];
static char     sp_status_buf[40];

/**
 * @brief 刷新底部状态行，显示三个 stepper 当前值。
 * @return 无。
 */
static void _sp_update_status(void)
{
    int32_t t = we_stepper_get_value(&sp_temp); /* 定点 ×10 */
    int32_t v = we_stepper_get_value(&sp_vol);
    int32_t a = we_stepper_get_value(&sp_ang);

    sprintf(sp_status_buf, "T:%ld.%ldC V:%ld A:%ld",
            (long)(t / 10), (long)(t % 10), (long)v, (long)a);
    we_label_set_text(&sp_status_label, sp_status_buf);
}

/**
 * @brief 数值改变回调：刷新状态行。
 * @param obj 触发回调的 stepper 对象。
 * @param value 新定点值。
 * @return 无。
 */
static void _sp_changed_cb(we_stepper_obj_t *obj, int32_t value)
{
    (void)obj;
    (void)value;
    _sp_update_status();
}

/**
 * @brief 初始化 stepper demo。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @return 无。
 */
void we_stepper_simple_demo_init(we_lcd_t *lcd)
{
    int16_t mx      = 10;
    int16_t title_y = 10;
    int16_t fps_x   = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    int16_t sp_w    = 130;
    int16_t sp_h    = 34;
    int16_t row0    = 44;
    int16_t row_gap = 46;
    int16_t cap_x   = (int16_t)(mx + sp_w + 12);

    sp_fps_timer   = 0U;
    sp_last_frames = 0U;
    memset(sp_fps_buf, 0, sizeof(sp_fps_buf));
    memset(sp_status_buf, 0, sizeof(sp_status_buf));

    we_label_obj_init(&sp_title, lcd, mx, title_y,
                      "STEPPER", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&sp_fps_label, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 温度：16.0~30.0，步进 0.5，1 位小数 */
    we_stepper_obj_init(&sp_temp, lcd, mx, row0, (uint16_t)sp_w, (uint16_t)sp_h,
                        we_font_consolas_18, 1U, 160, 300, 5, 230);
    we_stepper_set_changed_cb(&sp_temp, _sp_changed_cb);
    we_label_obj_init(&sp_temp_cap, lcd, cap_x, (int16_t)(row0 + 8),
                      "Temp C", we_font_consolas_18,
                      RGB888TODEV(160, 200, 245), 255);

    /* 音量：0~100，步进 5，纯整数 */
    we_stepper_obj_init(&sp_vol, lcd, mx, (int16_t)(row0 + row_gap),
                        (uint16_t)sp_w, (uint16_t)sp_h,
                        we_font_consolas_18, 0U, 0, 100, 5, 40);
    we_stepper_set_changed_cb(&sp_vol, _sp_changed_cb);
    we_label_obj_init(&sp_vol_cap, lcd, cap_x, (int16_t)(row0 + row_gap + 8),
                      "Volume", we_font_consolas_18,
                      RGB888TODEV(160, 200, 245), 255);

    /* 角度：0~359，步进 15，回绕 */
    we_stepper_obj_init(&sp_ang, lcd, mx, (int16_t)(row0 + 2 * row_gap),
                        (uint16_t)sp_w, (uint16_t)sp_h,
                        we_font_consolas_18, 0U, 0, 359, 15, 0);
    we_stepper_set_wrap(&sp_ang, 1U);
    we_stepper_set_changed_cb(&sp_ang, _sp_changed_cb);
    we_label_obj_init(&sp_ang_cap, lcd, cap_x, (int16_t)(row0 + 2 * row_gap + 8),
                      "Angle", we_font_consolas_18,
                      RGB888TODEV(160, 200, 245), 255);

    _sp_update_status();
    we_label_obj_init(&sp_status_label, lcd, mx, (int16_t)(row0 + 3 * row_gap + 4),
                      sp_status_buf, we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);
}

/**
 * @brief stepper demo 周期更新（控件本身事件驱动，这里只刷新 FPS）。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param ms_tick 传入：本轮累计毫秒数。
 * @return 无。
 */
void we_stepper_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &sp_fps_label, &sp_fps_timer,
                       &sp_last_frames, sp_fps_buf, ms_tick);
}
