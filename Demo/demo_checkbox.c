/**
 * @file  demo_checkbox.c
 * @brief Checkbox 控件完整功能 demo
 *
 * 演示内容：
 * 1. 多组 checkbox 点击切换
 * 2. 自定义颜色主题与 master 联动
 * 3. WiFi 文本周期轮换，tiny 方框程序自动 toggle
 * 4. 底部状态行实时汇总
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/checkbox/we_widget_checkbox.h"
#include <stdio.h>
#include <string.h>

static const we_cb_style_t _green_styles[WE_CB_STATE_MAX] = {
    [WE_CB_STATE_OFF]         = { .box_color = RGB888_CONST(100, 160, 100), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(200, 235, 200), .border_w = 2 },
    [WE_CB_STATE_OFF_PRESSED] = { .box_color = RGB888_CONST(130, 190, 130), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(220, 245, 220), .border_w = 2 },
    [WE_CB_STATE_ON]          = { .box_color = RGB888_CONST(40, 180, 80), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(200, 235, 200), .border_w = 0 },
    [WE_CB_STATE_ON_PRESSED]  = { .box_color = RGB888_CONST(30, 140, 60), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(220, 245, 220), .border_w = 0 },
};

static const we_cb_style_t _red_styles[WE_CB_STATE_MAX] = {
    [WE_CB_STATE_OFF]         = { .box_color = RGB888_CONST(160, 100, 100), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(235, 200, 200), .border_w = 2 },
    [WE_CB_STATE_OFF_PRESSED] = { .box_color = RGB888_CONST(190, 130, 130), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(245, 220, 220), .border_w = 2 },
    [WE_CB_STATE_ON]          = { .box_color = RGB888_CONST(220, 50, 50), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(235, 200, 200), .border_w = 0 },
    [WE_CB_STATE_ON_PRESSED]  = { .box_color = RGB888_CONST(180, 30, 30), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(245, 220, 220), .border_w = 0 },
};

static const we_cb_style_t _orange_styles[WE_CB_STATE_MAX] = {
    [WE_CB_STATE_OFF]         = { .box_color = RGB888_CONST(170, 140, 80), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(235, 220, 190), .border_w = 2 },
    [WE_CB_STATE_OFF_PRESSED] = { .box_color = RGB888_CONST(200, 170, 110), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(245, 235, 210), .border_w = 2 },
    [WE_CB_STATE_ON]          = { .box_color = RGB888_CONST(240, 160, 30), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(235, 220, 190), .border_w = 0 },
    [WE_CB_STATE_ON_PRESSED]  = { .box_color = RGB888_CONST(200, 130, 20), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(245, 235, 210), .border_w = 0 },
};

static const we_cb_style_t _purple_styles[WE_CB_STATE_MAX] = {
    [WE_CB_STATE_OFF]         = { .box_color = RGB888_CONST(130, 100, 160), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(215, 200, 235), .border_w = 2 },
    [WE_CB_STATE_OFF_PRESSED] = { .box_color = RGB888_CONST(160, 130, 190), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(230, 220, 245), .border_w = 2 },
    [WE_CB_STATE_ON]          = { .box_color = RGB888_CONST(140, 60, 220), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(215, 200, 235), .border_w = 0 },
    [WE_CB_STATE_ON_PRESSED]  = { .box_color = RGB888_CONST(110, 40, 180), .check_color = RGB888_CONST(255, 255, 255), .text_color = RGB888_CONST(230, 220, 245), .border_w = 0 },
};

static we_label_obj_t    cb_title;
static we_label_obj_t    cb_fps;
static we_label_obj_t    cb_status;
static we_label_obj_t    cb_hint;
static we_checkbox_obj_t cb_wifi;
static we_checkbox_obj_t cb_led;
static we_checkbox_obj_t cb_master;
static we_checkbox_obj_t cb_red;
static we_checkbox_obj_t cb_orange;
static we_checkbox_obj_t cb_purple;
static we_checkbox_obj_t cb_tiny;

static uint32_t cb_fps_timer;
static uint32_t cb_last_frames;
static uint32_t cb_tick_acc;
static char     cb_fps_buf[16];
static char     cb_status_buf[64];
static uint8_t  cb_last_snap;
static uint8_t  cb_text_phase;

/**
 * @brief 汇总各复选框状态并刷新底部状态行
 * @return 无
 */
static void _update_status(void)
{
    sprintf(cb_status_buf, "W:%d L:%d R:%d O:%d P:%d",
            we_checkbox_is_checked(&cb_wifi),
            we_checkbox_is_checked(&cb_led),
            we_checkbox_is_checked(&cb_red),
            we_checkbox_is_checked(&cb_orange),
            we_checkbox_is_checked(&cb_purple));
    we_label_set_text(&cb_status, cb_status_buf);
}

/**
 * @brief 处理 ALL 复选框事件并联动其他复选框
 * @param obj 传入：触发事件的复选框对象指针
 * @param event 传入：事件类型
 * @param data 传入：输入设备数据
 * @return 1 表示事件已消费
 */
static uint8_t _master_event_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    we_checkbox_obj_t *cb = (we_checkbox_obj_t *)obj;
    (void)data;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        cb->pressed = 1;
        we_obj_invalidate_area((we_obj_t *)cb, cb->base.x, cb->base.y, (int16_t)cb->box_size, (int16_t)cb->box_size);
        break;
    case WE_EVENT_RELEASED:
        cb->pressed = 0;
        we_obj_invalidate_area((we_obj_t *)cb, cb->base.x, cb->base.y, (int16_t)cb->box_size, (int16_t)cb->box_size);
        break;
    case WE_EVENT_CLICKED:
    {
        uint8_t val;
        we_checkbox_toggle(cb);
        val = we_checkbox_is_checked(cb);
        we_checkbox_set_checked(&cb_wifi, val);
        we_checkbox_set_checked(&cb_led, val);
        we_checkbox_set_checked(&cb_red, val);
        we_checkbox_set_checked(&cb_orange, val);
        we_checkbox_set_checked(&cb_purple, val);
        we_checkbox_set_checked(&cb_tiny, val);
        break;
    }
    default:
        break;
    }
    return 1;
}

/**
 * @brief 初始化 checkbox demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_checkbox_simple_demo_init(we_lcd_t *lcd)
{
    int16_t  mx      = 10;
    int16_t  title_y = 10;
    int16_t  hint_y  = 32;
    int16_t  fps_x   = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    int16_t  row_h   = 30;
    int16_t  start_y = 84;
    uint16_t box_sz  = 20;
    uint16_t box_sm  = 14;
    int16_t  col2_x  = 150;
    uint16_t box_c2  = 16;

    cb_fps_timer   = 0U;
    cb_last_frames = 0U;
    cb_tick_acc    = 0U;
    cb_last_snap   = 0xFFU;
    cb_text_phase  = 0U;
    memset(cb_fps_buf, 0, sizeof(cb_fps_buf));
    memset(cb_status_buf, 0, sizeof(cb_status_buf));

    we_label_obj_init(&cb_title, lcd, mx, title_y,
                      "CHECKBOX", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&cb_fps, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&cb_hint, lcd, mx, hint_y,
                      "click|style|link|states", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);

    we_checkbox_obj_init(&cb_wifi, lcd, mx, start_y, box_sz, "WiFi", we_font_consolas_18, NULL);
    we_checkbox_set_checked(&cb_wifi, 1);

    we_checkbox_obj_init(&cb_led, lcd, mx, (int16_t)(start_y + row_h), box_sz, "LED", we_font_consolas_18, NULL);
    we_checkbox_set_styles(&cb_led, _green_styles);

    we_checkbox_obj_init(&cb_master, lcd, mx, (int16_t)(start_y + row_h * 2), box_sz, "ALL", we_font_consolas_18, _master_event_cb);

    we_checkbox_obj_init(&cb_tiny, lcd, (int16_t)(mx + box_sz + 50), (int16_t)(start_y + row_h * 2 + (box_sz - box_sm) / 2), box_sm, NULL, NULL, NULL);

    we_checkbox_obj_init(&cb_red, lcd, col2_x, start_y, box_c2, "Alarm", we_font_consolas_18, NULL);
    we_checkbox_set_styles(&cb_red, _red_styles);
    we_checkbox_set_checked(&cb_red, 1);

    we_checkbox_obj_init(&cb_orange, lcd, col2_x, (int16_t)(start_y + row_h), box_c2, "Alert", we_font_consolas_18, NULL);
    we_checkbox_set_styles(&cb_orange, _orange_styles);

    we_checkbox_obj_init(&cb_purple, lcd, col2_x, (int16_t)(start_y + row_h * 2), box_c2, "Mode", we_font_consolas_18, NULL);
    we_checkbox_set_styles(&cb_purple, _purple_styles);
    we_checkbox_set_checked(&cb_purple, 1);

    _update_status();
    we_label_obj_init(&cb_status, lcd, mx, (int16_t)(start_y + row_h * 3 + 10), cb_status_buf, we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);
}

/**
 * @brief checkbox demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_checkbox_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint8_t snap;
    uint8_t new_phase;

    if (lcd == NULL || ms_tick == 0U)
        return;

    cb_tick_acc += ms_tick;

    new_phase = (uint8_t)((cb_tick_acc / 2000U) % 3U);
    if (new_phase != cb_text_phase)
    {
        cb_text_phase = new_phase;
        we_checkbox_toggle(&cb_tiny);
        switch (cb_text_phase)
        {
        case 0: we_checkbox_set_text(&cb_wifi, "WiFi"); break;
        case 1: we_checkbox_set_text(&cb_wifi, "WiFi 5G"); break;
        case 2: we_checkbox_set_text(&cb_wifi, "WLAN"); break;
        default: break;
        }
    }

    snap = (uint8_t)((we_checkbox_is_checked(&cb_wifi)   << 0) |
                     (we_checkbox_is_checked(&cb_led)    << 1) |
                     (we_checkbox_is_checked(&cb_red)    << 2) |
                     (we_checkbox_is_checked(&cb_orange) << 3) |
                     (we_checkbox_is_checked(&cb_purple) << 4) |
                     (we_checkbox_is_checked(&cb_master) << 5) |
                     (we_checkbox_is_checked(&cb_tiny)   << 6));

    if (snap != cb_last_snap)
    {
        cb_last_snap = snap;
        _update_status();
    }

    we_demo_update_fps(lcd, &cb_fps, &cb_fps_timer,
                       &cb_last_frames, cb_fps_buf, ms_tick);
}
