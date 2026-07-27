/**
 * @file  demo_tabview.c
 * @brief 页签条（tabview，preview）demo —— Home/Data/About 三页显隐切换
 *
 * 顶部一条 3 段页签条，下方 3 个 group 页容器（每页摆不同颜色的 box/label
 * 区分内容）。切换 tab 时在 changed_cb 里用 we_group_set_opacity 显示当前页、
 * 隐藏其它页——全透明 group 不拦截输入，正好让当前页正常交互。
 * 演示点：高亮块中央动画平滑滑动、值变回调、group 页容器显隐惯用法。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets/label/we_widget_label.h"
#include "widgets/group/we_widget_group.h"
#include "widgets/box/we_widget_box.h"
#include "widgets_preview/tabview/we_widget_tabview.h"
#include <string.h>

/* 页容器几何（280x240 布局） */
#define TV_PAGE_X 14
#define TV_PAGE_Y 80
#define TV_PAGE_W 252
#define TV_PAGE_H 148

static we_label_obj_t   tv_title;
static we_label_obj_t   tv_fps_label;
static we_tabview_obj_t tv_bar;
static we_group_obj_t   tv_page[3];

/* Home 页：一块圆角卡片 + 两行文字 */
static we_box_obj_t   tv_home_card;
static we_label_obj_t tv_home_head;
static we_label_obj_t tv_home_text;

/* Data 页：三根彩色柱 + 标题 */
static we_label_obj_t tv_data_head;
static we_box_obj_t   tv_data_bar[3];

/* About 页：三行说明文字 */
static we_label_obj_t tv_about_head;
static we_label_obj_t tv_about_line1;
static we_label_obj_t tv_about_line2;

static uint32_t tv_fps_timer;
static uint32_t tv_last_frames;
static char     tv_fps_buf[16];

/* 页签名数组：demo 静态持有，控件只存指针 */
static const char *const tv_tabs[3] = { "Home", "Data", "About" };

/**
 * @brief tab 切换回调：显示当前页 group、隐藏其它页
 * @param tv 传入：触发回调的页签条对象指针
 * @param idx 传入：新 active 段序号
 * @return 无
 */
static void _tv_tab_changed(void *tv, uint8_t idx)
{
    uint8_t i;

    (void)tv;
    for (i = 0U; i < 3U; i++)
        we_group_set_opacity(&tv_page[i], (uint8_t)((i == idx) ? 255U : 0U));
}

/**
 * @brief 初始化 tabview preview demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_tabview_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    colour_t page_bg = RGB888TODEV(24, 31, 43);
    uint8_t i;

    tv_fps_timer   = 0U;
    tv_last_frames = 0U;
    memset(tv_fps_buf, 0, sizeof(tv_fps_buf));

    we_label_obj_init(&tv_title, lcd, 14, 8,
                      "TABVIEW", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&tv_fps_label, lcd, fps_x, 8,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 顶部页签条：3 段等分，点击切换，高亮块平滑滑动 */
    we_tabview_obj_init(&tv_bar, lcd, 14, 34, 252, 34, tv_tabs, 3U, we_font_consolas_18);
    we_tabview_set_changed_cb(&tv_bar, _tv_tab_changed);

    /* 3 个页容器：初始只有第 0 页可见（透明 group 不拦输入） */
    for (i = 0U; i < 3U; i++)
        we_group_obj_init(&tv_page[i], lcd, TV_PAGE_X, TV_PAGE_Y,
                          TV_PAGE_W, TV_PAGE_H, page_bg,
                          (uint8_t)((i == 0U) ? 255U : 0U));

    /* ---- Home 页：蓝色圆角卡片 + 文案 ---- */
    we_label_obj_init(&tv_home_head, lcd, 0, 0,
                      "HOME PAGE", we_font_consolas_18,
                      RGB888TODEV(255, 154, 102), 255);
    we_box_obj_init(&tv_home_card, lcd, 0, 0, 150, 74);
    we_box_set_radius(&tv_home_card, 14U);
    we_box_set_color(&tv_home_card, RGB888TODEV(52, 96, 168));
    we_box_set_border(&tv_home_card, RGB888TODEV(120, 168, 224), 2U);
    we_label_obj_init(&tv_home_text, lcd, 0, 0,
                      "tap tabs above", we_font_consolas_18,
                      RGB888TODEV(220, 228, 238), 255);

    we_group_add_child(&tv_page[0], (we_obj_t *)&tv_home_head);
    we_group_add_child(&tv_page[0], (we_obj_t *)&tv_home_card);
    we_group_add_child(&tv_page[0], (we_obj_t *)&tv_home_text);
    we_group_set_child_pos(&tv_page[0], (we_obj_t *)&tv_home_head, 14, 10);
    we_group_set_child_pos(&tv_page[0], (we_obj_t *)&tv_home_card, 51, 38);
    we_group_set_child_pos(&tv_page[0], (we_obj_t *)&tv_home_text, 66, 118);

    /* ---- Data 页：三根不同颜色/高度的柱 ---- */
    we_label_obj_init(&tv_data_head, lcd, 0, 0,
                      "DATA PAGE", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_box_obj_init(&tv_data_bar[0], lcd, 0, 0, 40, 44);
    we_box_set_color(&tv_data_bar[0], RGB888TODEV(46, 140, 90));
    we_box_obj_init(&tv_data_bar[1], lcd, 0, 0, 40, 72);
    we_box_set_color(&tv_data_bar[1], RGB888TODEV(170, 104, 40));
    we_box_obj_init(&tv_data_bar[2], lcd, 0, 0, 40, 96);
    we_box_set_color(&tv_data_bar[2], RGB888TODEV(156, 62, 110));
    for (i = 0U; i < 3U; i++)
        we_box_set_radius(&tv_data_bar[i], 6U);

    we_group_add_child(&tv_page[1], (we_obj_t *)&tv_data_head);
    we_group_add_child(&tv_page[1], (we_obj_t *)&tv_data_bar[0]);
    we_group_add_child(&tv_page[1], (we_obj_t *)&tv_data_bar[1]);
    we_group_add_child(&tv_page[1], (we_obj_t *)&tv_data_bar[2]);
    we_group_set_child_pos(&tv_page[1], (we_obj_t *)&tv_data_head, 14, 10);
    /* 三柱底边对齐到局部 y=138 */
    we_group_set_child_pos(&tv_page[1], (we_obj_t *)&tv_data_bar[0], 36, 94);
    we_group_set_child_pos(&tv_page[1], (we_obj_t *)&tv_data_bar[1], 106, 66);
    we_group_set_child_pos(&tv_page[1], (we_obj_t *)&tv_data_bar[2], 176, 42);

    /* ---- About 页：三行说明文字 ---- */
    we_label_obj_init(&tv_about_head, lcd, 0, 0,
                      "ABOUT PAGE", we_font_consolas_18,
                      RGB888TODEV(168, 140, 230), 255);
    we_label_obj_init(&tv_about_line1, lcd, 0, 0,
                      "WeGui-ARGB preview", we_font_consolas_18,
                      RGB888TODEV(220, 228, 238), 255);
    we_label_obj_init(&tv_about_line2, lcd, 0, 0,
                      "tabview + group pages", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);

    we_group_add_child(&tv_page[2], (we_obj_t *)&tv_about_head);
    we_group_add_child(&tv_page[2], (we_obj_t *)&tv_about_line1);
    we_group_add_child(&tv_page[2], (we_obj_t *)&tv_about_line2);
    we_group_set_child_pos(&tv_page[2], (we_obj_t *)&tv_about_head, 14, 10);
    we_group_set_child_pos(&tv_page[2], (we_obj_t *)&tv_about_line1, 14, 52);
    we_group_set_child_pos(&tv_page[2], (we_obj_t *)&tv_about_line2, 14, 80);
}

/**
 * @brief tabview preview demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_tabview_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &tv_fps_label, &tv_fps_timer,
                       &tv_last_frames, tv_fps_buf, ms_tick);
}
