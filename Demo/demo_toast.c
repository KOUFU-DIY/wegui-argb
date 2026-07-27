/**
 * @file  demo_toast.c
 * @brief 轻提示（toast）控件功能 demo —— DEMO_ID 28，自动轮换 + 按钮即时弹出
 *
 * 内容：
 *   1. tick 每 2.2 秒轮换弹出一条 toast（5 条静态文案 + 各自底色，
 *      最后一条为超宽长文本，演示尾部自动截断加 "..."）
 *   2. 一个 btn：点击立即弹出一条紫色 toast——由于自动轮换仍在跑，
 *      经常出现"滑入/滑出动画进行中再次 show"，演示平滑重入不跳变
 *   3. 固定说明 label + FPS
 *
 * toast 非模态不挡输入：横幅盖住按钮时按钮仍可点击。
 */

#include "simple_widget_demos.h"
#include "demo_common.h"
#include "widgets/toast/we_widget_toast.h"
#include "widgets/btn/we_widget_btn.h"
#include "widgets/label/we_widget_label.h"
#include <string.h>

static we_label_obj_t ts_title;
static we_label_obj_t ts_note;
static we_label_obj_t ts_fps;
static we_btn_obj_t   ts_btn;
static we_toast_obj_t ts_toast;

static uint32_t ts_acc;         /* 自动轮换计时 */
static uint8_t  ts_idx;         /* 当前轮换文案序号 */
static uint32_t ts_fps_timer;
static uint32_t ts_last_frames;
static char     ts_fps_buf[16];

/* 自动轮换周期与单条停留时长（毫秒） */
#define TS_PERIOD   2200U
#define TS_DURATION 1200U

/* 轮换文案（静态持有，满足"调用方持有字符串"约定）与配套底色；
 * 最后一条超出横幅宽度，演示尾部自动截断加 "..." */
static const char *const ts_msg[5] = {
    "Saved successfully",
    "Wi-Fi disconnected",
    "3 new messages",
    "Volume set to 80%",
    "Firmware update 2.4.1 downloaded, restart the device to apply it",
};
static const uint8_t ts_bg[5][3] = {
    {  46, 140,  90 },   /* 绿：成功 */
    { 190,  70,  64 },   /* 红：告警 */
    {  52,  96, 168 },   /* 蓝：信息 */
    {  70,  78,  96 },   /* 灰：普通 */
    { 168, 120,  36 },   /* 琥珀：长文本省略号演示 */
};

/**
 * @brief 按钮回调：点击立即弹出一条紫色 toast（演示动画中重复 show）
 * @param obj 传入：按钮对象指针（未使用）
 * @param event 传入：输入事件类型
 * @param data 传入：输入设备数据指针（未使用）
 * @return 1 表示已消费
 */
static uint8_t _ts_btn_event_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    (void)obj;
    (void)data;

    if (event == WE_EVENT_CLICKED)
    {
        we_toast_set_colors(&ts_toast,
                            RGB888TODEV(120, 86, 200), RGB888TODEV(244, 240, 255));
        we_toast_show(&ts_toast, "Button toast: instant show()", 1500U);
    }
    return 1U;
}

/**
 * @brief 初始化 toast demo：说明/FPS/按钮 + 一个 toast 实例（初始隐藏）
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_toast_simple_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    /* 计时器预置到只差 200ms：首条 toast 在启动约 200ms 后即弹出 */
    ts_acc = (uint32_t)(TS_PERIOD - 200U);
    ts_idx = 0U;
    ts_fps_timer = 0U;
    ts_last_frames = 0U;
    memset(ts_fps_buf, 0, sizeof(ts_fps_buf));

    /* 标题放 y=48：顶部 8..44 让给 toast 停靠区，减少常驻文字被反复遮盖 */
    we_label_obj_init(&ts_title, lcd, 14, 48,
                      "TOAST", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&ts_note, lcd, 14, 74,
                      "auto: every 2.2s\nbtn: show() during anim", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&ts_fps, lcd, fps_x, 48,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    we_btn_obj_init(&ts_btn, lcd, 14, 150, 120, 36,
                    "SHOW NOW", we_font_consolas_18, _ts_btn_event_cb);

    /* toast 最后初始化：初始隐藏，show 时还会再 bring_to_front 置顶 */
    we_toast_obj_init(&ts_toast, lcd, we_font_consolas_18);
}

/**
 * @brief toast demo 周期更新：定时轮换弹出 + 刷 FPS
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_toast_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    ts_acc += ms_tick;
    if (ts_acc >= TS_PERIOD)
    {
        ts_acc = 0U;
        we_toast_set_colors(&ts_toast,
                            RGB888TODEV(ts_bg[ts_idx][0], ts_bg[ts_idx][1], ts_bg[ts_idx][2]),
                            RGB888TODEV(240, 244, 250));
        we_toast_show(&ts_toast, ts_msg[ts_idx], TS_DURATION);
        ts_idx = (uint8_t)((ts_idx + 1U) % 5U);
    }

    we_demo_update_fps(lcd, &ts_fps, &ts_fps_timer,
                       &ts_last_frames, ts_fps_buf, ms_tick);
}
