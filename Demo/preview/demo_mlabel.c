/**
 * @file  demo_mlabel.c
 * @brief 多行文本（mlabel）preview demo —— DEMO_ID 126
 *
 * 三块 mlabel 展示三种折行形态：
 *   A（左上，160x80）  —— 长英文段落，空格断词 + 超高末行 "..." 截断；
 *                         每 MLD_PERIOD 毫秒在两段文本间互换，演示流式重排。
 *   B（右列，92x180）  —— 中英混排 + 长无空格 token（字母表/URL），
 *                         窄列强制按字符断行。注意 we_font_consolas_18
 *                         实际字库仅覆盖 ASCII，CJK 码点解码正常但无字形、
 *                         按零宽跳过（验证解码安全），可见断行由 ASCII 长
 *                         token 演示。
 *   C（左下，160x90）  —— 显式 '\n' 四行短诗，行居中对齐。
 * 顶部说明 label 标注 A 块的轮换节奏。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/mlabel/we_widget_mlabel.h"
#include "widgets/label/we_widget_label.h"
#include <string.h>

static we_mlabel_obj_t mld_a; /* 断词段落（文本轮换） */
static we_mlabel_obj_t mld_b; /* 字符断行窄列 */
static we_mlabel_obj_t mld_c; /* 居中短诗 */
static we_label_obj_t  mld_title;
static we_label_obj_t  mld_fps_label;
static we_label_obj_t  mld_hint;   /* 说明 label */

static uint32_t mld_fps_timer;
static uint32_t mld_last_frames;
static char     mld_fps_buf[16];
static uint32_t mld_acc_ms;        /* A 块文本轮换计时器 */
static uint8_t  mld_a_alt;         /* 0 = 段落一，1 = 段落二 */

/* A 块文本轮换周期（毫秒） */
#define MLD_PERIOD 4000U

/* 布局（280x240 基准） */
#define MLD_A_X 10
#define MLD_A_Y 50
#define MLD_A_W 160
#define MLD_A_H 80
#define MLD_B_X 178
#define MLD_B_Y 50
#define MLD_B_W 92
#define MLD_B_H 180
#define MLD_C_X 10
#define MLD_C_Y 140
#define MLD_C_W 160
#define MLD_C_H 90

/* A 块两段互换文本：都超过 4 行容量，观察断词位置变化 + 末行 "..." */
static const char mld_text_a1[] =
    "WeGui mlabel wraps long English paragraphs at word boundaries, "
    "retreating to the nearest space when a line overflows the box width.";
static const char mld_text_a2[] =
    "Tiny embedded GUI kernels must budget every byte, so this preview "
    "widget re-flows its whole text layout on the fly, once per frame.";

/* B 块：中英混排 + 长无空格 token。CJK 无字形按零宽跳过（解码安全演示），
 * 字母表与 URL 两个长 token 在 92px 窄列里按字符硬切换行 */
static const char mld_text_b[] =
    "charwrap 中文按字符断行: "
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz "
    "https://wegui.dev/preview/mlabel";

/* C 块：显式 '\n' 四行短诗，行居中 */
static const char mld_text_c[] =
    "So much depends\nupon\na red wheel\nbarrow";

/**
 * @brief 初始化 mlabel preview demo。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @return 无。
 */
void we_mlabel_preview_demo_init(we_lcd_t *lcd)
{
    mld_fps_timer   = 0U;
    mld_last_frames = 0U;
    mld_acc_ms      = 0U;
    mld_a_alt       = 0U;
    memset(mld_fps_buf, 0, sizeof(mld_fps_buf));

    we_label_obj_init(&mld_title, lcd, 10, 8,
                      "MLABEL preview", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&mld_fps_label, lcd,
                      we_demo_fps_x(lcd, "FPS", we_font_consolas_18), 8,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&mld_hint, lcd, 10, 28,
                      "A swaps every 4s", we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    /* A：断词段落（默认左对齐 + ellipsis 开），周期性两段互换 */
    we_mlabel_obj_init(&mld_a, lcd, MLD_A_X, MLD_A_Y, MLD_A_W, MLD_A_H, mld_text_a1, we_font_consolas_18);
    we_mlabel_set_color(&mld_a, RGB888TODEV(220, 226, 235));

    /* B：窄列字符断行（长 token 无空格可退），列高溢出末行 "..." */
    we_mlabel_obj_init(&mld_b, lcd, MLD_B_X, MLD_B_Y, MLD_B_W, MLD_B_H, mld_text_b, we_font_consolas_18);
    we_mlabel_set_color(&mld_b, RGB888TODEV(250, 170, 100));

    /* C：居中短诗（显式 '\n' 分行，不触发自动折行） */
    we_mlabel_obj_init(&mld_c, lcd, MLD_C_X, MLD_C_Y, MLD_C_W, MLD_C_H, mld_text_c, we_font_consolas_18);
    we_mlabel_set_align(&mld_c, WE_MLABEL_CENTER);
    we_mlabel_set_color(&mld_c, RGB888TODEV(150, 230, 170));
}

/**
 * @brief mlabel preview demo 周期更新。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param ms_tick 传入：本轮累计毫秒数。
 * @return 无。
 */
void we_mlabel_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    /* A 块每 MLD_PERIOD 在两段文本间互换，触发整块流式重排 */
    mld_acc_ms += ms_tick;
    if (mld_acc_ms >= MLD_PERIOD)
    {
        mld_acc_ms = 0U;
        mld_a_alt ^= 1U;
        we_mlabel_set_text(&mld_a, mld_a_alt ? mld_text_a2 : mld_text_a1);
    }

    we_demo_update_fps(lcd, &mld_fps_label, &mld_fps_timer,
                       &mld_last_frames, mld_fps_buf, ms_tick);
}
