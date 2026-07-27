/**
 * @file  demo_hold_btn.c
 * @brief 长按确认按钮（hold_btn）preview demo —— 按住充能触发（DEMO_ID 124）
 *
 * 中央一个 120px "HOLD" 长按钮：按住外圈充能环逐段点亮，1.2s 充满触发；
 * 顶部状态 label 实时联动（idle 灰 / charging % 青 / TRIGGERED! 橙，
 * 触发瞬间由 triggered_cb 驱动，充能百分比在 tick 里轮询刷新）；
 * 右下角 RESET 小按钮复位锁定态。FPS 照常显示。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/hold_btn/we_widget_hold_btn.h"
#include "widgets/label/we_widget_label.h"
#include "widgets/btn/we_widget_btn.h"
#include <stdio.h>
#include <string.h>

/* 布局（280x240 基准） */
#define HB_BTN_SIZE 120
#define HB_BTN_X    ((280 - HB_BTN_SIZE) / 2)
#define HB_BTN_Y    64

static we_label_obj_t hb_title;
static we_label_obj_t hb_fps_label;
static we_label_obj_t hb_status_label; /* 顶部状态回显 */
static we_hold_btn_obj_t hb_btn;
static we_btn_obj_t hb_reset_btn;

static uint32_t hb_fps_timer;
static uint32_t hb_last_frames;
static char hb_fps_buf[16];
static char hb_status_buf[24];

static uint8_t hb_last_state; /* 0=idle 1=charging 2=triggered */
static int16_t hb_last_pct;   /* 上次显示的充能百分比 */

/**
 * @brief 刷新顶部状态 label（文字 + 颜色）。
 * @param state 传入：0=idle，1=charging，2=triggered
 * @param pct 传入：充能百分比（仅 charging 态使用）
 * @return 无
 */
static void hb_show_status(uint8_t state, int16_t pct)
{
    if (state == 2U)
    {
        sprintf(hb_status_buf, "TRIGGERED!");
        we_label_set_color(&hb_status_label, RGB888TODEV(255, 176, 60));
    }
    else if (state == 1U)
    {
        sprintf(hb_status_buf, "charging %d%%", (int)pct);
        we_label_set_color(&hb_status_label, RGB888TODEV(86, 205, 255));
    }
    else
    {
        sprintf(hb_status_buf, "idle");
        we_label_set_color(&hb_status_label, RGB888TODEV(150, 158, 170));
    }
    we_label_set_text(&hb_status_label, hb_status_buf);
}

/**
 * @brief 充满触发回调：立即切换状态显示（tick 轮询前先响应）。
 * @param hb 传入：触发的按钮对象指针（本 demo 未使用）
 * @return 无
 */
static void hb_on_triggered(void *hb)
{
    (void)hb;
    hb_last_state = 2U;
    hb_show_status(2U, 100);
}

/**
 * @brief RESET 小按钮事件回调：点击复位 hold_btn。
 * @param obj 传入：按钮对象指针（本 demo 未使用）
 * @param event 传入：输入事件类型
 * @param data 传入：输入数据（本 demo 未使用）
 * @return 恒返回 1
 */
static uint8_t hb_on_reset(void *obj, we_event_t event, we_indev_data_t *data)
{
    (void)obj;
    (void)data;
    if (event == WE_EVENT_CLICKED)
    {
        we_hold_btn_reset(&hb_btn);
        hb_last_state = 0U;
        hb_last_pct = -1;
        hb_show_status(0U, 0);
    }
    return 1U;
}

/**
 * @brief 初始化 hold_btn demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_hold_btn_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    hb_fps_timer = 0U;
    hb_last_frames = 0U;
    hb_last_state = 0U;
    hb_last_pct = -1;
    memset(hb_fps_buf, 0, sizeof(hb_fps_buf));
    memset(hb_status_buf, 0, sizeof(hb_status_buf));

    we_label_obj_init(&hb_title, lcd, 14, 10,
                      "HOLD_BTN", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&hb_fps_label, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* 顶部状态回显：初始 idle */
    sprintf(hb_status_buf, "idle");
    we_label_obj_init(&hb_status_label, lcd, 14, 36,
                      hb_status_buf, we_font_consolas_18,
                      RGB888TODEV(150, 158, 170), 255);

    /* 中央大长按钮：默认 1200ms 充满 */
    we_hold_btn_obj_init(&hb_btn, lcd, HB_BTN_X, HB_BTN_Y, HB_BTN_SIZE, "HOLD", we_font_consolas_18);
    we_hold_btn_set_triggered_cb(&hb_btn, hb_on_triggered);

    /* 右下 RESET 小按钮 */
    we_btn_obj_init(&hb_reset_btn, lcd, 196, 200, 70, 30,
                    "RESET", we_font_consolas_18, hb_on_reset);
}

/**
 * @brief hold_btn demo 周期更新：轮询充能进度刷新状态 label
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_hold_btn_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint8_t state;
    int16_t pct;

    if (lcd == NULL || ms_tick == 0U)
        return;

    /* 状态轮询：triggered 由回调即时切换，这里主要跟进 charging 百分比 */
    if (we_hold_btn_is_triggered(&hb_btn))
        state = 2U;
    else if (we_hold_btn_get_progress(&hb_btn) > 0U)
        state = 1U;
    else
        state = 0U;

    pct = (int16_t)(((uint32_t)we_hold_btn_get_progress(&hb_btn) * 100U) >> 8);

    if (state != hb_last_state || (state == 1U && pct != hb_last_pct))
    {
        hb_last_state = state;
        hb_last_pct = pct;
        hb_show_status(state, pct);
    }

    we_demo_update_fps(lcd, &hb_fps_label, &hb_fps_timer,
                       &hb_last_frames, hb_fps_buf, ms_tick);
}
