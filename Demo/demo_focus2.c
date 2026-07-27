/**
 * @file  demo_focus2.c
 * @brief 聚焦编辑态演示（DEMO_ID 30）：值类控件按键调值
 *
 * 演示内容：
 * 1. 方向键 / Tab 在 slider / stepper / roller / list 之间移动焦点（蓝色光标）
 * 2. Enter(OK) 进入编辑态：光标变橙色，方向键改为调值
 *    - slider：左右（或上下）按量程 1/20 步进
 *    - stepper：左右步进 ±0.5（复用触摸路径的边界/回绕逻辑）
 *    - roller：上下逐行吸附滚动（拉力+阻尼动画）
 *    - list：上下移动高亮行 + 滚动跟随，OK 选中该行（停留编辑态）
 * 3. Enter/Esc 退出编辑态回到导航；触摸点击可直接把焦点带过去
 * 4. 底部状态行显示最近一次值变化
 *
 * 模拟器按键映射（Simulator/sdl_port.c）：
 *   方向键 = 上/下/左/右   Tab / Shift+Tab = 后一个/前一个
 *   Enter / 空格 = OK      Esc / 退格 = BACK
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/slider/we_widget_slider.h"
#include "widgets/stepper/we_widget_stepper.h"
#include "widgets/roller/we_widget_roller.h"
#include "widgets/list/we_widget_list.h"
#include <stdio.h>
#include <string.h>

#if (WE_CFG_ENABLE_KEY_INPUT == 1)

static we_label_obj_t   f2_title;
static we_label_obj_t   f2_hint;
static we_label_obj_t   f2_fps;
static we_label_obj_t   f2_status;
static we_slider_obj_t  f2_slider;
static we_stepper_obj_t f2_stepper;
static we_roller_obj_t  f2_roller;
static we_list_obj_t    f2_list;

static uint32_t f2_fps_timer;
static uint32_t f2_last_frames;
static char     f2_fps_buf[16];
static char     f2_status_buf[32];

static const char *const f2_roller_opts[] = {
    "Red", "Green", "Blue", "Cyan", "Pink",
};
#define F2_ROLLER_CNT (sizeof(f2_roller_opts) / sizeof(f2_roller_opts[0]))

static const char *const f2_list_items[] = {
    "Wi-Fi", "Bluetooth", "Display", "Sound",
    "Battery", "Storage", "About", "Reboot",
};
#define F2_LIST_CNT (sizeof(f2_list_items) / sizeof(f2_list_items[0]))

/**
 * @brief slider 数值改变回调：刷新状态行
 */
static void _f2_slider_changed(void *obj, uint8_t value)
{
    (void)obj;
    snprintf(f2_status_buf, sizeof(f2_status_buf), "SLIDER %u", (unsigned)value);
    we_label_set_text(&f2_status, f2_status_buf);
}

/**
 * @brief stepper 数值改变回调（定点值，1 位小数）
 */
static void _f2_stepper_changed(struct we_stepper_obj_t *obj, int32_t value)
{
    (void)obj;
    snprintf(f2_status_buf, sizeof(f2_status_buf), "TEMP %ld.%ld",
             (long)(value / 10), (long)(value % 10));
    we_label_set_text(&f2_status, f2_status_buf);
}

/**
 * @brief roller 选中项改变回调（吸附完成时触发）
 */
static void _f2_roller_changed(struct we_roller_obj_t *obj, uint16_t selected_idx)
{
    (void)obj;
    snprintf(f2_status_buf, sizeof(f2_status_buf), "ROLLER %s",
             f2_roller_opts[selected_idx]);
    we_label_set_text(&f2_status, f2_status_buf);
}

/**
 * @brief list 行选中回调（编辑态 OK / 触摸点击行）
 */
static void _f2_list_clicked(void *list, uint16_t idx)
{
    (void)list;
    snprintf(f2_status_buf, sizeof(f2_status_buf), "LIST %s", f2_list_items[idx]);
    we_label_set_text(&f2_status, f2_status_buf);
}

/**
 * @brief 初始化聚焦编辑态演示场景
 * @param lcd GUI 运行时上下文
 */
void we_focus2_simple_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    f2_fps_timer   = 0U;
    f2_last_frames = 0U;
    memset(f2_fps_buf, 0, sizeof(f2_fps_buf));
    memset(f2_status_buf, 0, sizeof(f2_status_buf));

    we_label_obj_init(&f2_title, lcd, 10, 8, "FOCUS EDIT DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&f2_hint, lcd, 10, 30, "OK=edit  arrows=adjust", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&f2_fps, lcd, fps_x, 8, "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    we_slider_obj_init(&f2_slider, lcd, 10, 60, 150, 24, WE_SLIDER_ORIENT_HOR,
                       0, 100, 40,
                       RGB888TODEV(58, 66, 82), RGB888TODEV(92, 181, 255),
                       RGB888TODEV(255, 255, 255), 255);
    we_slider_set_changed_cb(&f2_slider, _f2_slider_changed);

    /* 温度 16.0~30.0、步进 0.5、1 位小数 */
    we_stepper_obj_init(&f2_stepper, lcd, 170, 56, 100, 32, we_font_consolas_18,
                        1, 160, 300, 5, 230);
    f2_stepper.changed_cb = _f2_stepper_changed;

    we_roller_obj_init(&f2_roller, lcd, 10, 96, 120, 3, we_font_consolas_18);
    we_roller_set_options(&f2_roller, f2_roller_opts, (uint16_t)F2_ROLLER_CNT);
    we_roller_set_changed_cb(&f2_roller, _f2_roller_changed);

    we_list_obj_init(&f2_list, lcd, 140, 96, 130, 108, we_font_consolas_18);
    we_list_set_options(&f2_list, f2_list_items, (uint16_t)F2_LIST_CNT);
    we_list_set_clicked_cb(&f2_list, _f2_list_clicked);

    we_label_obj_init(&f2_status, lcd, 10, 214, "Ready (OK enters edit mode)",
                      we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* 初始焦点落在 slider 上，光标立即可见 */
    we_focus_set(lcd, (we_obj_t *)&f2_slider);
}

/**
 * @brief 聚焦编辑态演示周期更新函数
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 */
void we_focus2_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;
    we_demo_update_fps(lcd, &f2_fps, &f2_fps_timer, &f2_last_frames, f2_fps_buf, ms_tick);
}

#else /* WE_CFG_ENABLE_KEY_INPUT == 0：降级桩，提示开启总开关 */

static we_label_obj_t f2_off_hint;

void we_focus2_simple_demo_init(we_lcd_t *lcd)
{
    we_label_obj_init(&f2_off_hint, lcd, 10, 10, "Set WE_CFG_ENABLE_KEY_INPUT=1",
                      we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
}

void we_focus2_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    (void)lcd;
    (void)ms_tick;
}

#endif /* WE_CFG_ENABLE_KEY_INPUT */
