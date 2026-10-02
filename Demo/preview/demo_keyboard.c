/**
 * @file  demo_keyboard.c
 * @brief 软键盘（keyboard，preview）demo —— 下半屏键盘 + 顶部 textarea 回显
 *
 * 键盘停靠下半屏，顶部放一个 textarea 输入框（preview 区配套控件）接收
 * 键值：敲键上屏、"<-" 退格删除、SH 切大写（敲一个字母自动回小写）、
 * 123/abc 切数字符号页。键值经 key_cb 直接透传给 we_textarea_input，
 * 演示两控件解耦对接的标准姿势。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets/label/we_widget_label.h"
#include "widgets_preview/keyboard/we_widget_keyboard.h"
#include "widgets_preview/textarea/we_widget_textarea.h"
#include <string.h>

#if !WE_DEMO_SMALL_SCREEN /* ==================== 280x240 大屏版 ==================== */

/* 回显缓冲上限（含结尾 0），写满后 textarea 忽略后续追加 */
#define KB_ECHO_BUF_MAX 64U

static we_label_obj_t    kb_title;
static we_label_obj_t    kb_fps_label;
static we_textarea_obj_t kb_echo;
static we_keyboard_obj_t kb_pad;

static uint32_t kb_fps_timer;
static uint32_t kb_last_frames;
static char     kb_fps_buf[16];
static char     kb_echo_buf[KB_ECHO_BUF_MAX];

/**
 * @brief 键值回调：把键面字符串直接透传给 textarea（"\b" 由其翻译为退格）
 * @param kb 传入：触发回调的键盘对象指针
 * @param key 传入：键面字符串（"\b"=退格，" "=空格）
 * @return 无
 */
static void _kb_demo_key_cb(void *kb, const char *key)
{
    (void)kb;
    we_textarea_input(&kb_echo, key);
}

/**
 * @brief 初始化 keyboard preview demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_keyboard_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    kb_fps_timer   = 0U;
    kb_last_frames = 0U;
    memset(kb_fps_buf, 0, sizeof(kb_fps_buf));
    memset(kb_echo_buf, 0, sizeof(kb_echo_buf));

    we_label_obj_init(&kb_title, lcd, 14, 8,
                      "KEYBOARD", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&kb_fps_label, lcd, fps_x, 8,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 顶部回显输入框：初始为空，显示占位提示（高度自动 = 行高 + 边距） */
    we_textarea_obj_init(&kb_echo, lcd, 14, 36, 252,
                         kb_echo_buf, (uint16_t)sizeof(kb_echo_buf), we_font_consolas_18);
    we_textarea_set_placeholder(&kb_echo, "Tap keys below...");
    we_textarea_set_editing(&kb_echo, 1U); /* 静态常显键盘：回显框保持编辑态光标 */

    /* 软键盘停靠下半屏：4 行 x 20 份网格，行高约 32px */
    we_keyboard_obj_init(&kb_pad, lcd, 4, 100, 272, 136, we_font_consolas_18);
    we_keyboard_set_key_cb(&kb_pad, _kb_demo_key_cb);
}

/**
 * @brief keyboard preview demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_keyboard_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &kb_fps_label, &kb_fps_timer,
                       &kb_last_frames, kb_fps_buf, ms_tick);
}

#else /* ==================== 128x64 小屏版 ==================== */

/* 小屏回显缓冲（label 直显，ASCII 单字节；满则整体左移丢最旧字符） */
#define KB_ECHO_BUF_MAX 15U

static we_label_obj_t    kb_echo_label;
static we_keyboard_obj_t kb_pad;

static char kb_echo_buf[KB_ECHO_BUF_MAX + 1U];

/**
 * @brief 键值回调：拼入回显缓冲（"\b" 退格，满则左移），刷新顶部 label
 * @param kb 传入：触发回调的键盘对象指针
 * @param key 传入：键面字符串（"\b"=退格，" "=空格）
 * @return 无
 */
static void _kb_demo_key_cb(void *kb, const char *key)
{
    size_t len = strlen(kb_echo_buf);

    (void)kb;
    if (key[0] == '\b')
    {
        if (len > 0U)
            kb_echo_buf[len - 1U] = '\0';
    }
    else
    {
        if (len >= KB_ECHO_BUF_MAX)
        {
            memmove(kb_echo_buf, kb_echo_buf + 1, len); /* 含结尾 0 一起前移 */
            len--;
        }
        kb_echo_buf[len] = key[0];
        kb_echo_buf[len + 1U] = '\0';
    }
    we_label_set_text(&kb_echo_label, kb_echo_buf[0] != '\0' ? kb_echo_buf : "_");
}

/**
 * @brief 初始化 keyboard preview demo（128x64 布局）
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_keyboard_preview_demo_init(we_lcd_t *lcd)
{
    memset(kb_echo_buf, 0, sizeof(kb_echo_buf));

    /* 顶部回显 label（textarea 高度放不下，小屏用 label 直显键值流） */
    we_label_obj_init(&kb_echo_label, lcd, 2, 0,
                      "_", we_font_consolas_18, RGB888TODEV(255, 255, 255), 255);

    /* 软键盘占满其余区域：4 行 x 20 份网格 */
    we_keyboard_obj_init(&kb_pad, lcd, 0, 13, 128, 51, we_font_consolas_18);
    we_keyboard_set_key_cb(&kb_pad, _kb_demo_key_cb);
}

/**
 * @brief keyboard preview demo 周期更新（交互全部事件驱动）
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_keyboard_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    (void)lcd;
    (void)ms_tick;
}

#endif /* WE_DEMO_SMALL_SCREEN */
