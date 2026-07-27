/**
 * @file  demo_focus.c
 * @brief 全局聚焦 + 按键导航演示（DEMO_ID 29）
 *
 * 演示内容：
 * 1. 方向键 / Tab(后一个) / Shift+Tab(前一个) 在同层控件间移动矩形焦点光标
 * 2. Enter(OK)：按钮按压闪烁并触发 CLICKED，checkbox/toggle 翻转，容器进入
 * 3. Esc(BACK)：从 group 内退回容器本体（光标框住整个 group），顶层再按清除焦点
 * 4. 底部状态行显示最近一次按键触发的动作
 *
 * 模拟器按键映射（Simulator/sdl_port.c）：
 *   方向键 = 上/下/左/右   Tab / Shift+Tab = 后一个/前一个
 *   Enter / 空格 = OK      Esc / 退格 = BACK
 * 触摸/鼠标操作与按键共存，互不影响。
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/btn/we_widget_btn.h"
#include "widgets/checkbox/we_widget_checkbox.h"
#include "widgets/group/we_widget_group.h"
#include "widgets/toggle/we_widget_toggle.h"
#include <string.h>

#if (WE_CFG_ENABLE_KEY_INPUT == 1)

static we_label_obj_t    focus_title;
static we_label_obj_t    focus_hint;
static we_label_obj_t    focus_fps;
static we_label_obj_t    focus_status;
static we_btn_obj_t      focus_btn_a;
static we_btn_obj_t      focus_btn_b;
static we_toggle_obj_t   focus_toggle;
static we_checkbox_obj_t focus_checkbox;
static we_group_obj_t    focus_group;
static we_btn_obj_t      focus_btn_g1;
static we_btn_obj_t      focus_btn_g2;
static we_toggle_obj_t   focus_toggle_g;

static uint32_t focus_fps_timer;
static uint32_t focus_last_frames;
static char     focus_fps_buf[16];

/**
 * @brief 更新底部状态行文本
 * @param text 状态文本（常量字符串，控件只存指针）
 */
static void _focus_show_status(const char *text)
{
    we_label_set_text(&focus_status, text);
}

/**
 * @brief 按钮用户回调：CLICKED 时按对象区分显示状态
 * @note 按键合成的 CLICKED 事件 data 为 NULL，这里不解引用坐标。
 */
static uint8_t _focus_btn_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    (void)data;
    if (event != WE_EVENT_CLICKED)
        return 1U;
    if (obj == (void *)&focus_btn_a)
        _focus_show_status("BTN A clicked");
    else if (obj == (void *)&focus_btn_b)
        _focus_show_status("BTN B clicked");
    else if (obj == (void *)&focus_btn_g1)
        _focus_show_status("Group BTN 1 clicked");
    else
        _focus_show_status("Group BTN 2 clicked");
    return 1U;
}

/**
 * @brief 开关状态改变回调（顶层与 group 内共用）
 */
static void _focus_toggle_changed(void *obj, uint8_t checked)
{
    if (obj == (void *)&focus_toggle)
        _focus_show_status(checked ? "Toggle: ON" : "Toggle: OFF");
    else
        _focus_show_status(checked ? "G-Toggle: ON" : "G-Toggle: OFF");
}

/**
 * @brief 勾选框状态改变回调
 */
static void _focus_checkbox_changed(void *obj, uint8_t checked)
{
    (void)obj;
    _focus_show_status(checked ? "Sound: ON" : "Sound: OFF");
}

/**
 * @brief 初始化聚焦演示场景
 * @param lcd GUI 运行时上下文
 */
void we_focus_simple_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    focus_fps_timer   = 0U;
    focus_last_frames = 0U;
    memset(focus_fps_buf, 0, sizeof(focus_fps_buf));

    we_label_obj_init(&focus_title, lcd, 10, 8, "FOCUS DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&focus_hint, lcd, 10, 30, "Arrow/Tab Enter Esc", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&focus_fps, lcd, fps_x, 8, "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 顶层控件行：两个按钮 + 开关 + 勾选框 */
    we_btn_obj_init(&focus_btn_a, lcd, 10, 56, 74, 30, "BTN A", we_font_consolas_18, _focus_btn_cb);
    we_btn_obj_init(&focus_btn_b, lcd, 94, 56, 74, 30, "BTN B", we_font_consolas_18, _focus_btn_cb);
    we_toggle_obj_init(&focus_toggle, lcd, 182, 58, 52, 26, NULL);
    we_toggle_set_changed_cb(&focus_toggle, _focus_toggle_changed);
    we_checkbox_obj_init(&focus_checkbox, lcd, 10, 96, 20, "Sound", we_font_consolas_18, NULL);
    we_checkbox_set_changed_cb(&focus_checkbox, _focus_checkbox_changed);

    /* group 容器：聚焦后 OK 进入子控件、Esc 退回容器本体 */
    we_group_obj_init(&focus_group, lcd, 10, 128, 260, 74, RGB888TODEV(30, 38, 52), 255);
    we_btn_obj_init(&focus_btn_g1, lcd, 0, 0, 70, 28, "G-BTN 1", we_font_consolas_18, _focus_btn_cb);
    we_btn_obj_init(&focus_btn_g2, lcd, 0, 0, 70, 28, "G-BTN 2", we_font_consolas_18, _focus_btn_cb);
    we_toggle_obj_init(&focus_toggle_g, lcd, 0, 0, 52, 26, NULL);
    we_toggle_set_changed_cb(&focus_toggle_g, _focus_toggle_changed);
    we_group_add_child(&focus_group, (we_obj_t *)&focus_btn_g1);
    we_group_add_child(&focus_group, (we_obj_t *)&focus_btn_g2);
    we_group_add_child(&focus_group, (we_obj_t *)&focus_toggle_g);
    we_group_set_child_pos(&focus_group, (we_obj_t *)&focus_btn_g1, 12, 23);
    we_group_set_child_pos(&focus_group, (we_obj_t *)&focus_btn_g2, 96, 23);
    we_group_set_child_pos(&focus_group, (we_obj_t *)&focus_toggle_g, 188, 24);

    we_label_obj_init(&focus_status, lcd, 10, 214, "Ready (keys control focus)", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 初始焦点落在第一个按钮上，光标立即可见 */
    we_focus_set(lcd, (we_obj_t *)&focus_btn_a);
}

/**
 * @brief 聚焦演示周期更新函数
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 */
void we_focus_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;
    we_demo_update_fps(lcd, &focus_fps, &focus_fps_timer, &focus_last_frames, focus_fps_buf, ms_tick);
}

#else /* WE_CFG_ENABLE_KEY_INPUT == 0：降级桩，提示开启总开关 */

static we_label_obj_t focus_off_hint;

void we_focus_simple_demo_init(we_lcd_t *lcd)
{
    we_label_obj_init(&focus_off_hint, lcd, 10, 10, "Set WE_CFG_ENABLE_KEY_INPUT=1",
                      we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
}

void we_focus_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    (void)lcd;
    (void)ms_tick;
}

#endif /* WE_CFG_ENABLE_KEY_INPUT */
