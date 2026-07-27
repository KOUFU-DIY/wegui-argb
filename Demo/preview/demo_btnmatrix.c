/**
 * @file  demo_btnmatrix.c
 * @brief 按键矩阵（btnmatrix，preview）demo —— 4x3 数字键盘 + 顶部回显
 *
 * 一个 4 行 3 列的数字键盘（"1".."9","*","0","#"），点击任意键把键名字符
 * 追加进静态缓冲并更新顶部回显 label；缓冲写满后清空重来。
 * 演示点：数据驱动键名数组、格子按压高亮、拖出取消、点击回调。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets/label/we_widget_label.h"
#include "widgets_preview/btnmatrix/we_widget_btnmatrix.h"
#include <string.h>

/* 回显缓冲上限（含结尾 0），写满清空重来 */
#define BM_ECHO_MAX 14U

static we_label_obj_t     bm_title;
static we_label_obj_t     bm_fps_label;
static we_label_obj_t     bm_echo;
static we_btnmatrix_obj_t bm_pad;

static uint32_t bm_fps_timer;
static uint32_t bm_last_frames;
static char     bm_fps_buf[16];
static char     bm_echo_buf[BM_ECHO_MAX];

/* 键名数组（行优先 4x3）：demo 静态持有，控件只存指针 */
static const char *const bm_keys[12] = {
    "1", "2", "3",
    "4", "5", "6",
    "7", "8", "9",
    "*", "0", "#"
};

/**
 * @brief 按键点击回调：把键名追加进回显缓冲（写满清空重来）
 * @param bm 传入：触发回调的按键矩阵对象指针
 * @param key_idx 传入：行优先格序号
 * @param label 传入：对应键名字符串
 * @return 无
 */
static void _bm_key_clicked(void *bm, uint8_t key_idx, const char *label)
{
    size_t len;
    size_t add;

    (void)bm;
    (void)key_idx;
    if (label == NULL)
        return;

    len = strlen(bm_echo_buf);
    add = strlen(label);
    if (len + add >= sizeof(bm_echo_buf))
    {
        /* 超长：清空重来，本次按键作为新内容的第一个字符 */
        bm_echo_buf[0] = '\0';
        len = 0U;
    }
    memcpy(&bm_echo_buf[len], label, add + 1U);

    we_label_set_text(&bm_echo, bm_echo_buf);
}

/**
 * @brief 初始化 btnmatrix preview demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_btnmatrix_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    bm_fps_timer   = 0U;
    bm_last_frames = 0U;
    memset(bm_fps_buf, 0, sizeof(bm_fps_buf));
    memset(bm_echo_buf, 0, sizeof(bm_echo_buf));

    we_label_obj_init(&bm_title, lcd, 14, 8,
                      "BTNMATRIX", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&bm_fps_label, lcd, fps_x, 8,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 顶部回显：初始为空缓冲，点击后实时刷新 */
    we_label_obj_init(&bm_echo, lcd, 14, 32,
                      bm_echo_buf, we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    /* 4x3 数字键盘：格宽 (170-2*5)/3=53，格高 (176-3*5)/4=40 */
    we_btnmatrix_obj_init(&bm_pad, lcd, 55, 56, 170, 176, bm_keys, 4U, 3U, we_font_consolas_18);
    we_btnmatrix_set_clicked_cb(&bm_pad, _bm_key_clicked);
}

/**
 * @brief btnmatrix preview demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_btnmatrix_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &bm_fps_label, &bm_fps_timer,
                       &bm_last_frames, bm_fps_buf, ms_tick);
}
