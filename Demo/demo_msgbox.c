/**
 * @file  demo_msgbox.c
 * @brief 顶部滑入消息框演示
 *
 * 演示内容：
 * 1. 自动轮换单按钮和双按钮消息框
 * 2. 状态行显示当前自动流程阶段
 * 3. 右上角 FPS
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/label/we_widget_label.h"
#include "widgets/msgbox/we_widget_msgbox.h"
#include <string.h>

static we_label_obj_t popup_title;
static we_label_obj_t popup_hint;
static we_label_obj_t popup_fps;
static we_label_obj_t popup_status;

static we_msgbox_obj_t popup_ok_box;
static we_msgbox_obj_t popup_ok_cancel_box;

static uint32_t popup_fps_timer;
static uint32_t popup_last_frames;
static uint32_t popup_demo_timer;
static uint8_t  popup_demo_stage;
static char     popup_fps_buf[16];
static char     popup_status_buf[48];

/**
 * @brief 更新底部状态行文本
 * @param text 传入：状态字符串
 * @return 无
 */
static void _popup_set_status(const char *text)
{
    if (text == NULL)
        return;
    strncpy(popup_status_buf, text, sizeof(popup_status_buf) - 1U);
    popup_status_buf[sizeof(popup_status_buf) - 1U] = '\0';
    we_label_set_text(&popup_status, popup_status_buf);
}

/**
 * @brief 隐藏所有消息框
 * @return 无
 */
static void _popup_hide_all(void)
{
    we_msgbox_hide(&popup_ok_box);
    we_msgbox_hide(&popup_ok_cancel_box);
}

/**
 * @brief msgbox_ok 的 OK 回调
 * @param obj 传入：消息框对象指针
 * @return 无
 */
static void _popup_ok_cb(we_msgbox_obj_t *obj)
{
    _popup_set_status("AUTO HANDLE: msgbox_ok -> OK");
    we_msgbox_hide(obj);
}

/**
 * @brief msgbox_ok_cancel 的 OK 回调
 * @param obj 传入：消息框对象指针
 * @return 无
 */
static void _popup_ok_cancel_ok_cb(we_msgbox_obj_t *obj)
{
    _popup_set_status("AUTO HANDLE: msgbox_ok_cancel -> OK");
    we_msgbox_hide(obj);
}

/**
 * @brief msgbox_ok_cancel 的 CANCEL 回调
 * @param obj 传入：消息框对象指针
 * @return 无
 */
static void _popup_ok_cancel_cancel_cb(we_msgbox_obj_t *obj)
{
    _popup_set_status("AUTO HANDLE: msgbox_ok_cancel -> CANCEL");
    we_msgbox_hide(obj);
}

/**
 * @brief 初始化 msgbox demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_msgbox_simple_demo_init(we_lcd_t *lcd)
{
    popup_fps_timer   = 0U;
    popup_last_frames = 0U;
    popup_demo_timer  = 0U;
    popup_demo_stage  = 0U;
    memset(popup_fps_buf, 0, sizeof(popup_fps_buf));
    memset(popup_status_buf, 0, sizeof(popup_status_buf));

    we_label_obj_init(&popup_title, lcd, 10, 10,
                      "MSGBOX", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&popup_fps, lcd, 196, 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&popup_hint, lcd, 10, 32,
                      "auto trigger | top-in", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);

    strncpy(popup_status_buf, "WAIT: auto trigger msgbox_ok", sizeof(popup_status_buf) - 1U);
    popup_status_buf[sizeof(popup_status_buf) - 1U] = '\0';
    we_label_obj_init(&popup_status, lcd, 10, 196,
                      popup_status_buf, we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    we_msgbox_ok_obj_init(&popup_ok_box, lcd, 176, 112, 56,
                          "NOTICE", "Only confirm action",
                          "OK",
                          we_font_consolas_18, we_font_consolas_18, we_font_consolas_18,
                          _popup_ok_cb);

    we_msgbox_ok_cancel_obj_init(&popup_ok_cancel_box, lcd, 188, 116, 56,
                                 "DELETE FILE?", "Cancel or confirm",
                                 "OK", "CANCEL",
                                 we_font_consolas_18, we_font_consolas_18, we_font_consolas_18,
                                 _popup_ok_cancel_ok_cb, _popup_ok_cancel_cancel_cb);
}

/**
 * @brief msgbox demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_msgbox_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    popup_demo_timer += ms_tick;

    switch (popup_demo_stage)
    {
    case 0:
        if (popup_demo_timer >= 700U)
        {
            popup_demo_timer = 0U;
            popup_demo_stage = 1U;
            _popup_hide_all();
            _popup_set_status("SHOW: msgbox_ok");
            we_msgbox_show(&popup_ok_box);
        }
        break;
    case 1:
        if (popup_demo_timer >= 1500U)
        {
            popup_demo_timer = 0U;
            popup_demo_stage = 2U;
            _popup_ok_cb(&popup_ok_box);
        }
        break;
    case 2:
        if (popup_demo_timer >= 900U)
        {
            popup_demo_timer = 0U;
            popup_demo_stage = 3U;
            _popup_hide_all();
            _popup_set_status("SHOW: msgbox_ok_cancel");
            we_msgbox_show(&popup_ok_cancel_box);
        }
        break;
    case 3:
        if (popup_demo_timer >= 1500U)
        {
            popup_demo_timer = 0U;
            popup_demo_stage = 4U;
            _popup_ok_cancel_cancel_cb(&popup_ok_cancel_box);
        }
        break;
    default:
        if (popup_demo_timer >= 1100U)
        {
            popup_demo_timer = 0U;
            popup_demo_stage = 0U;
            _popup_set_status("WAIT: auto trigger msgbox_ok");
        }
        break;
    }

    we_demo_update_fps(lcd, &popup_fps, &popup_fps_timer,
                       &popup_last_frames, popup_fps_buf, ms_tick);
}
