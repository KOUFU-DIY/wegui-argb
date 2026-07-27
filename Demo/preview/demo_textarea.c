/**
 * @file  demo_textarea.c
 * @brief 单行输入框 + 弹层软键盘（preview）demo —— 完整文本输入链路
 *
 * 两个 textarea 共享一个弹层软键盘（单例）：
 *   触摸路径：点击输入框 → 键盘自屏底滑入 → 点键入字（SH/123 换页、
 *             "<-" 退格）→ "OK" 提交并收回 / 点键盘上方区域收回；
 *   按键路径：Tab 聚焦输入框 → OK 呼出键盘 → 方向键移动键光标
 *             （行内回绕、跨行就近落键）→ OK 双沿击键 → BACK 收回。
 * "OK" 确定经 done_cb 把提交内容回显到底部 label。
 * 演示点：placeholder 初始态、光标闪烁、溢出左移滚动、滑入/收回动画。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets/label/we_widget_label.h"
#include "widgets_preview/textarea/we_widget_textarea.h"
#include "widgets_preview/keyboard/we_widget_keyboard.h"
#include <string.h>

/* 文本缓冲上限（含结尾 0）：39 字符约在 18 字符处开始溢出滚动 */
#define TA_TEXT_BUF_MAX 40U

/* 弹层键盘面板高度（屏高 240：滑入后覆盖 108..239，两个输入框都不遮挡） */
#define TA_KB_H 132

static we_label_obj_t    ta_title;
static we_label_obj_t    ta_fps_label;
static we_label_obj_t    ta_hint;
static we_label_obj_t    ta_echo;
static we_textarea_obj_t ta_name;
static we_textarea_obj_t ta_note;
static we_keyboard_obj_t ta_kb;

static uint32_t ta_fps_timer;
static uint32_t ta_last_frames;
static char     ta_fps_buf[16];
static char     ta_name_buf[TA_TEXT_BUF_MAX];
static char     ta_note_buf[TA_TEXT_BUF_MAX];
static char     ta_echo_buf[TA_TEXT_BUF_MAX + 6];

/**
 * @brief "OK" 确定回调：把提交的输入框内容回显到底部 label
 * @param kb 传入：键盘对象指针（未用）
 * @param target 传入：show 时绑定的目标输入框（we_textarea_obj_t*）
 * @return 无
 */
static void _ta_kb_done_cb(void *kb, void *target)
{
    const char *text;

    (void)kb;
    if (target == NULL)
        return;
    text = we_textarea_get_text((const we_textarea_obj_t *)target);
    if (text == NULL)
        return;

    memcpy(ta_echo_buf, "OK: ", 4U);
    strncpy(&ta_echo_buf[4], text, sizeof(ta_echo_buf) - 5U);
    ta_echo_buf[sizeof(ta_echo_buf) - 1U] = '\0';
    we_label_set_text(&ta_echo, ta_echo_buf);
}

/**
 * @brief 初始化 textarea + 弹层键盘 preview demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_textarea_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    ta_fps_timer   = 0U;
    ta_last_frames = 0U;
    memset(ta_fps_buf, 0, sizeof(ta_fps_buf));
    memset(ta_name_buf, 0, sizeof(ta_name_buf));
    memset(ta_note_buf, 0, sizeof(ta_note_buf));
    memset(ta_echo_buf, 0, sizeof(ta_echo_buf));

    we_label_obj_init(&ta_title, lcd, 14, 8,
                      "TEXTAREA + KB", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&ta_fps_label, lcd, fps_x, 8,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 两个输入框：NAME 故意取窄（170px）演示溢出左移滚动 */
    we_textarea_obj_init(&ta_name, lcd, 14, 44, 170,
                         ta_name_buf, (uint16_t)sizeof(ta_name_buf), we_font_consolas_18);
    we_textarea_set_placeholder(&ta_name, "Name...");

    we_textarea_obj_init(&ta_note, lcd, 14, 78, 252,
                         ta_note_buf, (uint16_t)sizeof(ta_note_buf), we_font_consolas_18);
    we_textarea_set_placeholder(&ta_note, "Tap to edit...");

    we_label_obj_init(&ta_hint, lcd, 14, 112,
                      "tap box (or focus + OK)", we_font_consolas_18,
                      RGB888TODEV(122, 131, 146), 255);
    we_label_obj_init(&ta_echo, lcd, 14, 136,
                      "", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 弹层软键盘（单例）：绑定两个输入框，呼出时以触发框为注入目标 */
    we_keyboard_popup_init(&ta_kb, lcd, TA_KB_H, we_font_consolas_18);
    we_keyboard_set_done_cb(&ta_kb, _ta_kb_done_cb);
    we_textarea_bind_keyboard(&ta_name, &ta_kb);
    we_textarea_bind_keyboard(&ta_note, &ta_kb);
}

/**
 * @brief textarea preview demo 周期更新：仅刷新 FPS
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_textarea_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &ta_fps_label, &ta_fps_timer,
                       &ta_last_frames, ta_fps_buf, ms_tick);
}
