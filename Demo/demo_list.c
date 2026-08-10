/**
 * @file  demo_list.c
 * @brief 数据驱动列表（list）控件功能 demo —— 10 项设置菜单（DEMO_ID 25）
 *
 * 一块 200x160 列表面板承载 10 项设置菜单（static const 字符串数组，
 * 调用方持有，控件只存指针）。内容超出面板高度，可体验完整交互：
 * 拖拽跟手 + 松手惯性、快速轻扫（无停顿快甩）同样带惯性、拖过头
 * 橡皮筋过冲后回弹、右缘滚动条活动全显 / 空闲自动渐隐到常驻低透明；
 * 点击某行把该项名称回显到顶部 label。FPS 照常显示。
 */

#include "simple_widget_demos.h"
#include "demo_common.h"
#include "widgets/list/we_widget_list.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t ls_title;
static we_label_obj_t ls_fps_label;
static we_label_obj_t ls_sel_label;   /* 顶部点击回显 */
static we_list_obj_t ls_menu;

static uint32_t ls_fps_timer;
static uint32_t ls_last_frames;
static char ls_fps_buf[16];
static char ls_sel_buf[24];

/* 布局（280x240 基准） */
#define LS_PANEL_X 40
#define LS_PANEL_Y 64
#define LS_PANEL_W 200
#define LS_PANEL_H 160

/* 10 项设置菜单（调用方持有的静态数组，控件只存指针） */
static const char *const ls_items[] = {
    "Display",
    "Sound",
    "Network",
    "Bluetooth",
    "Battery",
    "Storage",
    "Security",
    "Language",
    "Date & Time",
    "About",
};

/**
 * @brief 行点击回调：把被点行的名称回显到顶部 label。
 * @param list 传入：列表控件对象指针（void * 透传，本 demo 未使用）
 * @param idx 传入：被点击行的条目索引
 * @return 无
 */
static void ls_on_clicked(void *list, uint16_t idx)
{
    (void)list;
    if (idx >= 10U)
        return;
    sprintf(ls_sel_buf, "> %s", ls_items[idx]);
    we_label_set_text(&ls_sel_label, ls_sel_buf);
}

/**
 * @brief 初始化 list demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_list_simple_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    ls_fps_timer = 0U;
    ls_last_frames = 0U;
    memset(ls_fps_buf, 0, sizeof(ls_fps_buf));
    memset(ls_sel_buf, 0, sizeof(ls_sel_buf));

    we_label_obj_init(&ls_title, lcd, 14, 10,
                      "LIST", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&ls_fps_label, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* 顶部回显：初始提示文案 */
    sprintf(ls_sel_buf, "> tap an item");
    we_label_obj_init(&ls_sel_label, lcd, 14, 36,
                      ls_sel_buf, we_font_consolas_18,
                      RGB888TODEV(112, 184, 255), 255);

    /* 列表面板：10 项内容超出 160px 高度，可滚动；绑定条目后滚动条
     * 会短暂全显提示"此处可滚动"，随后自动渐隐（空闲淡出默认开启） */
    we_list_obj_init(&ls_menu, lcd, LS_PANEL_X, LS_PANEL_Y, LS_PANEL_W, LS_PANEL_H,
                     we_font_consolas_18);
    we_list_set_options(&ls_menu, ls_items, (uint16_t)(sizeof(ls_items) / sizeof(ls_items[0])));
    we_list_set_clicked_cb(&ls_menu, ls_on_clicked);
}

/**
 * @brief list demo 周期更新（仅刷新 FPS，列表交互全部事件驱动）
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_list_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &ls_fps_label, &ls_fps_timer,
                       &ls_last_frames, ls_fps_buf, ms_tick);
}
