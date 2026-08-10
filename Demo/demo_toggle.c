/**
 * @file  demo_toggle.c
 * @brief 拨动开关控件（Toggle Switch）完整功能 demo
 *
 * 演示内容：
 * 1. 三个独立开关 + 一个 ALL 联动开关
 * 2. Dark Mode 定时自动切换
 * 3. 底部状态行实时显示四个开关状态
 */

#include "simple_widget_demos.h"
#include "demo_common.h"
#include "widgets/toggle/we_widget_toggle.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t  tgl_title;
static we_label_obj_t  tgl_fps_label;
static we_label_obj_t  tgl_hint;
static we_label_obj_t  tgl_status_label;
static we_label_obj_t  tgl_wifi_lbl;
static we_label_obj_t  tgl_bt_lbl;
static we_label_obj_t  tgl_dark_lbl;
static we_label_obj_t  tgl_all_lbl;
static we_toggle_obj_t tgl_wifi;
static we_toggle_obj_t tgl_bt;
static we_toggle_obj_t tgl_dark;
static we_toggle_obj_t tgl_all;

static uint32_t tgl_fps_timer;
static uint32_t tgl_last_frames;
static uint32_t tgl_tick_acc;
static uint32_t tgl_auto_timer;
static uint8_t  tgl_last_snap;
static char     tgl_fps_buf[16];
static char     tgl_status_buf[32];

/**
 * @brief 打包四个开关当前状态为位图快照
 * @return 状态位图（bit0~bit3 对应四个开关）
 */
static uint8_t _snap(void)
{
    return (uint8_t)((we_toggle_is_checked(&tgl_wifi) << 0) |
                     (we_toggle_is_checked(&tgl_bt) << 1) |
                     (we_toggle_is_checked(&tgl_dark) << 2) |
                     (we_toggle_is_checked(&tgl_all) << 3));
}

/**
 * @brief 刷新底部开关状态文本
 * @return 无
 */
static void _update_status(void)
{
    sprintf(tgl_status_buf, "W:%d BT:%d DK:%d All:%d",
            we_toggle_is_checked(&tgl_wifi),
            we_toggle_is_checked(&tgl_bt),
            we_toggle_is_checked(&tgl_dark),
            we_toggle_is_checked(&tgl_all));
    we_label_set_text(&tgl_status_label, tgl_status_buf);
}

/**
 * @brief 处理 ALL 开关事件并联动前三个开关
 * @param obj 传入：触发事件的开关对象指针
 * @param event 传入：事件类型
 * @param data 传入：输入设备数据
 * @return 1 表示事件已消费
 */
static uint8_t _all_event_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    we_toggle_obj_t *t = (we_toggle_obj_t *)obj;
    (void)data;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        t->pressed = 1U;
        we_obj_invalidate((we_obj_t *)t);
        break;
    case WE_EVENT_RELEASED:
        t->pressed = 0U;
        we_obj_invalidate((we_obj_t *)t);
        break;
    case WE_EVENT_CLICKED:
    {
        uint8_t val;
        we_toggle_toggle(t);
        val = we_toggle_is_checked(t);
        we_toggle_set_checked(&tgl_wifi, val);
        we_toggle_set_checked(&tgl_bt, val);
        we_toggle_set_checked(&tgl_dark, val);
        break;
    }
    default:
        break;
    }
    return 1;
}

/**
 * @brief 初始化 toggle demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_toggle_simple_demo_init(we_lcd_t *lcd)
{
    int16_t mx       = 10;
    int16_t title_y  = 10;
    int16_t hint_y   = 32;
    int16_t fps_x    = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    int16_t row_h    = 38;
    int16_t start_y  = 84;
    int16_t tgl_w    = 44;
    int16_t tgl_h    = 24;
    int16_t lbl_x    = (int16_t)(mx + tgl_w + 12);
    int16_t tgl_y_off = (int16_t)((row_h - tgl_h) / 2);

    tgl_fps_timer   = 0U;
    tgl_last_frames = 0U;
    tgl_tick_acc    = 0U;
    tgl_auto_timer  = 0U;
    tgl_last_snap   = 0xFFU;
    memset(tgl_fps_buf, 0, sizeof(tgl_fps_buf));
    memset(tgl_status_buf, 0, sizeof(tgl_status_buf));

    we_label_obj_init(&tgl_title, lcd, mx, title_y,
                      "TOGGLE", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&tgl_fps_label, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&tgl_hint, lcd, mx, hint_y,
                      "tap|anim|link|auto", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);

    /* 第 1 行：WiFi */
    {
        int16_t row_y = start_y;
        int16_t lbl_y;
        int8_t  yt, yb;

        we_toggle_obj_init(&tgl_wifi, lcd, mx, (int16_t)(row_y + tgl_y_off), tgl_w, tgl_h, NULL);
        we_get_text_bbox(we_font_consolas_18, "WiFi", &yt, &yb);
        lbl_y = (int16_t)(row_y + row_h / 2) - (yt + yb) / 2;
        we_label_obj_init(&tgl_wifi_lbl, lcd, lbl_x, lbl_y,
                          "WiFi", we_font_consolas_18,
                          RGB888TODEV(220, 228, 238), 255);
    }

    /* 第 2 行：Bluetooth */
    {
        int16_t row_y = (int16_t)(start_y + row_h);
        int16_t lbl_y;
        int8_t  yt, yb;

        we_toggle_obj_init(&tgl_bt, lcd, mx, (int16_t)(row_y + tgl_y_off), tgl_w, tgl_h, NULL);
        we_get_text_bbox(we_font_consolas_18, "Bluetooth", &yt, &yb);
        lbl_y = (int16_t)(row_y + row_h / 2) - (yt + yb) / 2;
        we_label_obj_init(&tgl_bt_lbl, lcd, lbl_x, lbl_y,
                          "Bluetooth", we_font_consolas_18,
                          RGB888TODEV(220, 228, 238), 255);
    }

    /* 第 3 行：Dark Mode */
    {
        int16_t row_y = (int16_t)(start_y + row_h * 2);
        int16_t lbl_y;
        int8_t  yt, yb;

        we_toggle_obj_init(&tgl_dark, lcd, mx, (int16_t)(row_y + tgl_y_off), tgl_w, tgl_h, NULL);
        we_get_text_bbox(we_font_consolas_18, "Dark Mode", &yt, &yb);
        lbl_y = (int16_t)(row_y + row_h / 2) - (yt + yb) / 2;
        we_label_obj_init(&tgl_dark_lbl, lcd, lbl_x, lbl_y,
                          "Dark Mode", we_font_consolas_18,
                          RGB888TODEV(220, 228, 238), 255);
    }

    we_toggle_set_checked(&tgl_bt, 1);

    /* 第 4 行：ALL 联动开关（自定义回调联动前三个） */
    {
        int16_t row_y = (int16_t)(start_y + row_h * 3);
        int16_t lbl_y;
        int8_t  yt, yb;

        we_toggle_obj_init(&tgl_all, lcd, mx, (int16_t)(row_y + tgl_y_off), tgl_w, tgl_h, _all_event_cb);
        we_get_text_bbox(we_font_consolas_18, "ALL", &yt, &yb);
        lbl_y = (int16_t)(row_y + row_h / 2) - (yt + yb) / 2;
        we_label_obj_init(&tgl_all_lbl, lcd, lbl_x, lbl_y,
                          "ALL", we_font_consolas_18,
                          RGB888TODEV(245, 214, 120), 255);
    }

    _update_status();
    we_label_obj_init(&tgl_status_label, lcd, mx, (int16_t)(start_y + row_h * 4 + 4),
                      tgl_status_buf, we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);
}

/**
 * @brief toggle demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_toggle_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint8_t snap;

    if (lcd == NULL || ms_tick == 0U)
        return;

    tgl_tick_acc += ms_tick;
    tgl_auto_timer += ms_tick;

    if (tgl_auto_timer >= 2500U)
    {
        tgl_auto_timer = 0U;
        we_toggle_toggle(&tgl_dark);
    }

    snap = _snap();
    if (snap != tgl_last_snap)
    {
        tgl_last_snap = snap;
        _update_status();
    }

    we_demo_update_fps(lcd, &tgl_fps_label, &tgl_fps_timer,
                       &tgl_last_frames, tgl_fps_buf, ms_tick);
}
