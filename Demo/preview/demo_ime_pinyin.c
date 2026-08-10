/**
 * @file  demo_ime_pinyin.c
 * @brief 拼音输入法（ime_pinyin，preview）demo —— textarea + IME 面板 + 自动演示
 *
 * 布局自上而下：标题/FPS、textarea 输入框（点击呼出弹层输入法）、
 * 弹层 IME（回显条 + 拼音条 + 候选栏 + 内嵌键盘，屏底滑入收回；
 * 点面板上方 / BACK 收回，收回后点输入框再次呼出）。
 *
 * 自动演示脚本（录 GIF 用，无人工输入）：tick 累计毫秒驱动步进表循环
 * ——逐键敲 "ni"→选"你"、"hao"→选"好"、"shi"→翻页展示后选"世"、
 * "jie"→选"界"，停顿后退格清空重来。脚本经 we_ime_pinyin_inject_key /
 * select / page 喂给控件（与真实触摸同一入口，不注入系统鼠标），
 * 手动触摸全程可用。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets/label/we_widget_label.h"
#include "widgets_preview/ime_pinyin/we_widget_ime_pinyin.h"
#include "widgets_preview/textarea/we_widget_textarea.h"
#include "msyh_16_4bpp_ime.h"
#include <string.h>

/* 含中文的 IME 字库（ASCII + GB2312 一级 3755 字，16px 4bpp internal） */
#define IME_DEMO_FONT ((const unsigned char *)&msyh_16_4bpp_ime)

/* 回显缓冲上限（含结尾 0；一个 CJK 字 3 字节） */
#define IME_ECHO_BUF_MAX 64U

/* 自动脚本步进间隔（毫秒，任务书要求 400~600） */
#define IME_STEP_MS 500U

/* 自动脚本单目标最多向后翻页数（翻尽仍未找到则兜底选第 1 候选） */
#define IME_PICK_PAGE_MAX 6U

static we_label_obj_t      ime_title;
static we_label_obj_t      ime_fps_label;
static we_textarea_obj_t   ime_echo;
static we_ime_pinyin_obj_t ime_pad;

static uint32_t ime_fps_timer;
static uint32_t ime_last_frames;
static char     ime_fps_buf[16];
static char     ime_echo_buf[IME_ECHO_BUF_MAX];

/* ---------------- 自动演示脚本 ---------------- */

#define IME_OP_KEY    0U /* 注入一个键值（字母/退格） */
#define IME_OP_PICK   1U /* 在候选页中找目标码点并点选（找不到自动向后翻页） */
#define IME_OP_PAGE_F 2U /* 候选栏向后翻一页（展示 ">"） */
#define IME_OP_PAGE_B 3U /* 候选栏回翻一页（展示 "<"） */
#define IME_OP_PAUSE  4U /* 停一拍 */

typedef struct
{
    uint8_t op;   /* IME_OP_xxx */
    char key[2];  /* IME_OP_KEY 的键面（单字符 / "\b"） */
    uint16_t cp;  /* IME_OP_PICK 的目标 Unicode 码点 */
} ime_demo_step_t;

/* 循环脚本：你好世界 -> 退格清空 -> 重来 */
static const ime_demo_step_t ime_steps[] = {
    { IME_OP_KEY, "n", 0U },      { IME_OP_KEY, "i", 0U },
    { IME_OP_PICK, "", 0x4F60U }, /* 你 */
    { IME_OP_KEY, "h", 0U },      { IME_OP_KEY, "a", 0U }, { IME_OP_KEY, "o", 0U },
    { IME_OP_PICK, "", 0x597DU }, /* 好 */
    { IME_OP_KEY, "s", 0U },      { IME_OP_KEY, "h", 0U }, { IME_OP_KEY, "i", 0U },
    { IME_OP_PAGE_F, "", 0U },    { IME_OP_PAGE_B, "", 0U }, /* 秀一下翻页键 */
    { IME_OP_PICK, "", 0x4E16U }, /* 世 */
    { IME_OP_KEY, "j", 0U },      { IME_OP_KEY, "i", 0U }, { IME_OP_KEY, "e", 0U },
    { IME_OP_PICK, "", 0x754CU }, /* 界 */
    { IME_OP_PAUSE, "", 0U },     { IME_OP_PAUSE, "", 0U },
    { IME_OP_KEY, "\b", 0U },     { IME_OP_KEY, "\b", 0U },
    { IME_OP_KEY, "\b", 0U },     { IME_OP_KEY, "\b", 0U },
    { IME_OP_PAUSE, "", 0U },
};
#define IME_STEP_NUM (sizeof(ime_steps) / sizeof(ime_steps[0]))

static uint16_t ime_step_idx;   /* 当前脚本步 */
static uint16_t ime_step_acc;   /* 步进毫秒累计 */
static uint8_t  ime_pick_pages; /* 当前 PICK 已向后翻页数 */

/* 弹层模式绑定目标后由 IME 直接注入输入框，无需 commit 胶水回调 */

/**
 * @brief 执行一步自动脚本
 * @return 1 = 本步完成（步进游标前移），0 = 本步未完成（PICK 翻页中，下拍重试）
 */
static uint8_t _ime_demo_run_step(void)
{
    const ime_demo_step_t *st = &ime_steps[ime_step_idx];

    switch (st->op)
    {
    case IME_OP_KEY:
        we_ime_pinyin_inject_key(&ime_pad, st->key);
        break;

    case IME_OP_PICK:
    {
        uint8_t i;

        for (i = 0U; i < ime_pad.page_cnt; i++)
        {
            if (ime_pad.page_cp[i] == st->cp)
            {
                (void)we_ime_pinyin_select(&ime_pad, i);
                ime_pick_pages = 0U;
                return 1U;
            }
        }
        /* 本页没有目标字：向后翻页下拍重试；翻尽兜底选第 1 候选 */
        if (ime_pick_pages < IME_PICK_PAGE_MAX && we_ime_pinyin_page(&ime_pad, 1))
        {
            ime_pick_pages++;
            return 0U;
        }
        (void)we_ime_pinyin_select(&ime_pad, 0U);
        ime_pick_pages = 0U;
        break;
    }

    case IME_OP_PAGE_F:
        (void)we_ime_pinyin_page(&ime_pad, 1);
        break;

    case IME_OP_PAGE_B:
        (void)we_ime_pinyin_page(&ime_pad, -1);
        break;

    default: /* IME_OP_PAUSE：什么也不做，停一拍 */
        break;
    }
    return 1U;
}

/**
 * @brief 初始化 ime_pinyin preview demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_ime_pinyin_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    ime_fps_timer   = 0U;
    ime_last_frames = 0U;
    ime_step_idx    = 0U;
    ime_step_acc    = 0U;
    ime_pick_pages  = 0U;
    memset(ime_fps_buf, 0, sizeof(ime_fps_buf));
    memset(ime_echo_buf, 0, sizeof(ime_echo_buf));

    we_label_obj_init(&ime_title, lcd, 14, 5,
                      "IME PINYIN", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&ime_fps_label, lcd, fps_x, 5,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 输入框：点击呼出弹层输入法（含中文字库，按其行高重算高度） */
    we_textarea_obj_init(&ime_echo, lcd, 14, 30, 252,
                         ime_echo_buf, (uint16_t)sizeof(ime_echo_buf), IME_DEMO_FONT);
    ime_echo.base.h = (int16_t)(we_font_get_line_height(IME_DEMO_FONT) + 2 * WE_TEXTAREA_PAD_Y);
    we_textarea_set_placeholder(&ime_echo, "Tap to type pinyin...");

    /* 弹层输入法：回显 26 + 拼音条 22 + 候选栏 30 + 键盘 124 = 202，
     * 绑定输入框后初始即滑入（演示/录 GIF 直接可见；收回后点框再呼出） */
    we_ime_pinyin_popup_init(&ime_pad, lcd, 202, IME_DEMO_FONT);
    we_ime_pinyin_bind_textarea(&ime_echo, &ime_pad);
    we_ime_pinyin_popup_show(&ime_pad, &ime_echo);
}

/**
 * @brief ime_pinyin preview demo 周期更新（FPS + 自动演示脚本步进）
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_ime_pinyin_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &ime_fps_label, &ime_fps_timer,
                       &ime_last_frames, ime_fps_buf, ms_tick);

    /* 自动演示脚本只在弹层展开期间步进（收回后暂停，点输入框恢复） */
    if (we_modal_get(lcd) != (we_obj_t *)&ime_pad)
        return;

    ime_step_acc = (uint16_t)(ime_step_acc + ms_tick);
    if (ime_step_acc >= IME_STEP_MS)
    {
        ime_step_acc = 0U;
        if (_ime_demo_run_step())
            ime_step_idx = (uint16_t)((ime_step_idx + 1U) % IME_STEP_NUM);
    }
}
