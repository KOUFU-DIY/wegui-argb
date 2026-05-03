/**
 * @file  demo_key.c
 * @brief 单按键 + GUI 事件响应 demo
 *
 * 演示内容：
 * 1. 读取 PA6 电平并直接驱动上方按钮状态
 * 2. 松开 PA6 时累计硬件按键次数
 * 3. 下方按钮使用 GUI 事件回调演示 PRESSED / RELEASED / CLICKED
 * 4. 右上角 FPS
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/btn/we_widget_btn.h"
#include <stdio.h>
#include <string.h>

#ifndef WE_SIMULATOR
#include "stm32f10x.h"

/**
 * @brief 初始化按键 GPIO（PA6 上拉输入）
 * @return 无
 */
static void _key_gpio_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    gpio.GPIO_Pin   = GPIO_Pin_6;
    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);
}

/**
 * @brief 读取按键状态
 * @return 1 表示按下，0 表示松开
 */
static uint8_t _key_read(void)
{
    return (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_6) == Bit_RESET) ? 1U : 0U;
}
#else
static void    _key_gpio_init(void) {}
static uint8_t _key_read(void)      { return 0U; }
#endif

static we_label_obj_t key_title;
static we_label_obj_t key_fps;
static we_label_obj_t key_hint;
static we_label_obj_t key_gpio_stat;
static we_label_obj_t key_event_stat;
static we_btn_obj_t   key_gpio_btn;
static we_btn_obj_t   key_event_btn;

static uint32_t key_fps_timer;
static uint32_t key_last_frames;
static uint32_t key_gpio_count;
static uint32_t key_event_clicks;
static uint8_t  key_last;
static char     key_fps_buf[16];
static char     key_gpio_buf[24];
static char     key_event_buf[32];

/**
 * @brief 刷新硬件按键统计文本
 * @return 无
 */
static void _update_gpio_stat(void)
{
    sprintf(key_gpio_buf, "PA6 CNT %03lu", (unsigned long)key_gpio_count);
    we_label_set_text(&key_gpio_stat, key_gpio_buf);
}

/**
 * @brief 刷新 GUI 事件统计文本
 * @param evt_name 传入：事件名称字符串
 * @return 无
 */
static void _update_event_stat(const char *evt_name)
{
    sprintf(key_event_buf, "EVT %-8s CLK %03lu", evt_name, (unsigned long)key_event_clicks);
    we_label_set_text(&key_event_stat, key_event_buf);
}

/**
 * @brief 处理测试按钮事件并更新显示状态
 * @param obj 传入：按钮对象指针
 * @param event 传入：事件类型
 * @param data 传入：输入设备数据
 * @return 1 表示事件已消费
 */
static uint8_t _key_event_btn_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    we_btn_obj_t *btn = (we_btn_obj_t *)obj;
    (void)data;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        we_btn_set_state(btn, WE_BTN_STATE_PRESSED);
        we_btn_set_text(btn, "PRESSED");
        _update_event_stat("PRESS");
        break;
    case WE_EVENT_RELEASED:
        we_btn_set_state(btn, WE_BTN_STATE_NORMAL);
        we_btn_set_text(btn, "RELEASE");
        _update_event_stat("RELEASE");
        break;
    case WE_EVENT_CLICKED:
        key_event_clicks++;
        we_btn_set_text(btn, "CLICKED");
        _update_event_stat("CLICK");
        break;
    default:
        break;
    }
    return 1U;
}

/**
 * @brief 初始化 key demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_key_simple_demo_init(we_lcd_t *lcd)
{
    int16_t margin_x = 10;
    int16_t title_y  = 10;
    int16_t hint_y   = 32;
    int16_t fps_x    = 196;
    int16_t btn_w    = 140;
    int16_t btn_h    = 36;
    int16_t btn_x    = (int16_t)((lcd->width - btn_w) / 2);

    _key_gpio_init();

    key_fps_timer    = 0U;
    key_last_frames  = 0U;
    key_gpio_count   = 0U;
    key_event_clicks = 0U;
    key_last         = 0U;
    memset(key_fps_buf, 0, sizeof(key_fps_buf));
    memset(key_gpio_buf, 0, sizeof(key_gpio_buf));
    memset(key_event_buf, 0, sizeof(key_event_buf));

    we_label_obj_init(&key_title, lcd, margin_x, title_y,
                      "KEY DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&key_hint, lcd, margin_x, hint_y,
                      "PA6 + GUI event callback", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&key_fps, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    strcpy(key_gpio_buf, "PA6 CNT 000");
    we_label_obj_init(&key_gpio_stat, lcd, margin_x, 54,
                      key_gpio_buf, we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    we_btn_obj_init(&key_gpio_btn, lcd, btn_x, 82, btn_w, btn_h,
                    "PA10 IDLE", we_font_consolas_18, NULL);
    we_btn_set_radius(&key_gpio_btn, 12U);

    we_btn_obj_init(&key_event_btn, lcd, btn_x, 138, btn_w, btn_h,
                    "CLICK TEST", we_font_consolas_18, _key_event_btn_cb);
    we_btn_set_radius(&key_event_btn, 12U);

    strcpy(key_event_buf, "EVT NONE     CLK 000");
    we_label_obj_init(&key_event_stat, lcd, margin_x, 192,
                      key_event_buf, we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
}

/**
 * @brief key demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_key_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint8_t cur;

    if (lcd == NULL || ms_tick == 0U)
        return;

    cur = _key_read();

    if (cur != key_last)
    {
        key_last = cur;

        if (cur == 1U)
        {
            we_btn_set_state(&key_gpio_btn, WE_BTN_STATE_PRESSED);
            we_btn_set_text(&key_gpio_btn, "PA6 DOWN");
        }
        else
        {
            we_btn_set_state(&key_gpio_btn, WE_BTN_STATE_NORMAL);
            we_btn_set_text(&key_gpio_btn, "PA6 IDLE");
            key_gpio_count++;
            _update_gpio_stat();
        }
    }

    we_demo_update_fps(lcd, &key_fps, &key_fps_timer,
                       &key_last_frames, key_fps_buf, ms_tick);
}
