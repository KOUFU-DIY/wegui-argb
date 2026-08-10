/**
 * @file  demo_dropdown.c
 * @brief 下拉选择控件（Dropdown）完整功能 demo
 *
 * 演示内容：
 * 1. 顶部一个短列表 dropdown（向下展开）
 * 2. 底部一个长列表 dropdown（靠近屏幕底部，自动向上展开 + 滚动）
 * 3. 选中改变时刷新底部状态行
 * 4. 展开列表作为顶层模态对象绘制，压在所有普通控件之上，不被裁剪
 */

#include "simple_widget_demos.h"
#include "demo_common.h"
#include "widgets/dropdown/we_widget_dropdown.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t    dd_title;
static we_label_obj_t    dd_fps_label;
static we_label_obj_t    dd_hint;
static we_label_obj_t    dd_status_label;
static we_dropdown_obj_t dd_top;
static we_dropdown_obj_t dd_bottom;

static uint32_t dd_fps_timer;
static uint32_t dd_last_frames;
static char     dd_fps_buf[16];
static char     dd_status_buf[40];

/* 短列表：主题模式 */
static const we_dropdown_option_t dd_mode_opts[] = {
    {"Auto",  0, 0},
    {"Light", 1, 0},
    {"Dark",  2, 0},
};

/* 长列表：采样率，含一个禁用项，验证滚动与禁用渲染 */
static const we_dropdown_option_t dd_rate_opts[] = {
    {"10 Hz",   10,  0},
    {"25 Hz",   25,  0},
    {"50 Hz",   50,  0},
    {"100 Hz",  100, 0},
    {"200 Hz",  200, 1}, /* 禁用 */
    {"500 Hz",  500, 0},
    {"1 kHz",   1000, 0},
};

/**
 * @brief 刷新底部状态行，显示两个 dropdown 当前选中值。
 * @return 无。
 */
static void _dd_update_status(void)
{
    int16_t mi = we_dropdown_get_selected(&dd_top);
    int16_t ri = we_dropdown_get_selected(&dd_bottom);
    const char *mode = (mi >= 0) ? dd_mode_opts[mi].text : "-";

    sprintf(dd_status_buf, "Mode:%s  Rate:%ldHz",
            mode, (long)we_dropdown_get_value(&dd_bottom));
    (void)ri;
    we_label_set_text(&dd_status_label, dd_status_buf);
}

/**
 * @brief 选中改变回调：刷新底部状态行。
 * @param obj 触发回调的 dropdown 对象。
 * @param selected_idx 新选中项索引。
 * @param value 新选中项关联值。
 * @return 无。
 */
static void _dd_changed_cb(we_dropdown_obj_t *obj, int16_t selected_idx, int32_t value)
{
    (void)obj;
    (void)selected_idx;
    (void)value;
    _dd_update_status();
}

/**
 * @brief 初始化 dropdown demo。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @return 无。
 */
void we_dropdown_simple_demo_init(we_lcd_t *lcd)
{
    int16_t mx      = 10;
    int16_t title_y = 10;
    int16_t hint_y  = 32;
    int16_t fps_x   = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    int16_t dd_w    = 140;
    int16_t dd_h    = 30;
    int16_t top_y   = 70;
    int16_t bot_y   = (int16_t)(lcd->height - 38); /* 贴近底部，强制向上展开 */

    dd_fps_timer   = 0U;
    dd_last_frames = 0U;
    memset(dd_fps_buf, 0, sizeof(dd_fps_buf));
    memset(dd_status_buf, 0, sizeof(dd_status_buf));

    we_label_obj_init(&dd_title, lcd, mx, title_y,
                      "DROPDOWN", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&dd_fps_label, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&dd_hint, lcd, mx, hint_y,
                      "tap to expand", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);

    /* 顶部：短列表，向下展开 */
    we_dropdown_obj_init(&dd_top, lcd, mx, top_y, dd_w, dd_h, we_font_consolas_18);
    we_dropdown_set_options(&dd_top, dd_mode_opts,
                            (uint16_t)(sizeof(dd_mode_opts) / sizeof(dd_mode_opts[0])));
    we_dropdown_set_selected(&dd_top, 0);
    we_dropdown_set_changed_cb(&dd_top, _dd_changed_cb);

    /* 底部：长列表，靠近底部自动向上展开 + 滚动 */
    we_dropdown_obj_init(&dd_bottom, lcd, mx, bot_y, dd_w, dd_h, we_font_consolas_18);
    we_dropdown_set_options(&dd_bottom, dd_rate_opts,
                            (uint16_t)(sizeof(dd_rate_opts) / sizeof(dd_rate_opts[0])));
    we_dropdown_set_max_visible_items(&dd_bottom, 4);
    we_dropdown_set_selected(&dd_bottom, 2);
    we_dropdown_set_changed_cb(&dd_bottom, _dd_changed_cb);

    _dd_update_status();
    we_label_obj_init(&dd_status_label, lcd, mx, (int16_t)(top_y + dd_h + 16),
                      dd_status_buf, we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);
}

/**
 * @brief dropdown demo 周期更新（控件本身事件驱动，这里只刷新 FPS）。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param ms_tick 传入：本轮累计毫秒数。
 * @return 无。
 */
void we_dropdown_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &dd_fps_label, &dd_fps_timer,
                       &dd_last_frames, dd_fps_buf, ms_tick);
}


