/**
 * @file  demo_menu.c
 * @brief 多级菜单（menu）preview demo —— 三层设置菜单树（DEMO_ID 103）
 *
 * 一块 220x170 菜单面板承载三层菜单树（全部 static const，调用方持有，
 * 控件只存指针）：根页 5 项（Display/Sound/Network/System/About，前四项
 * 带子页）→ 子页各 5~7 项（部分再带一层孙页选项）→ 叶子行点击时 action
 * 回调把 "label (id)" 回显到顶部 label。
 *
 * 交互：点带 ">" 的行进子页（右滑入过渡）；点标题栏 "<" 或在行区
 * 向右滑/拖 → 返回上一级；行区可拖拽滚动 + 松手惯性；每页各自记住
 * 滚动位置。FPS 照常显示。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/menu/we_widget_menu.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t mn_title;
static we_label_obj_t mn_fps_label;
static we_label_obj_t mn_sel_label; /* 顶部动作回显 */
static we_menu_obj_t mn_menu;

static uint32_t mn_fps_timer;
static uint32_t mn_last_frames;
static char mn_fps_buf[16];
static char mn_sel_buf[32];

/* 布局（280x240 基准） */
#define MN_PANEL_X 30
#define MN_PANEL_Y 60
#define MN_PANEL_W 220
#define MN_PANEL_H 170

/* ------------------------- 菜单树（调用方持有） -------------------------
 * 三层结构，自底向上定义：孙页 → 子页 → 根页。
 * 动作 ID 分段：1xx Display / 2xx Sound / 3xx Network / 4xx System / 900 About
 * ------------------------------------------------------------------------ */

/* --- 第三层（孙页） --- */
static const we_menu_item_t mn_brightness_items[] = {
    { "25%",  NULL, 110U },
    { "50%",  NULL, 111U },
    { "75%",  NULL, 112U },
    { "100%", NULL, 113U },
};
static const we_menu_page_t mn_page_brightness = { "Brightness", mn_brightness_items, 4U };

static const we_menu_item_t mn_timeout_items[] = {
    { "15 sec", NULL, 130U },
    { "30 sec", NULL, 131U },
    { "1 min",  NULL, 132U },
    { "5 min",  NULL, 133U },
};
static const we_menu_page_t mn_page_timeout = { "Sleep Timeout", mn_timeout_items, 4U };

static const we_menu_item_t mn_volume_items[] = {
    { "Mute",   NULL, 210U },
    { "Low",    NULL, 211U },
    { "Medium", NULL, 212U },
    { "High",   NULL, 213U },
};
static const we_menu_page_t mn_page_volume = { "Volume", mn_volume_items, 4U };

static const we_menu_item_t mn_wifi_items[] = {
    { "Home-AP",   NULL, 310U },
    { "Office-5G", NULL, 311U },
    { "Cafe-Free", NULL, 312U },
    { "Lab-IoT",   NULL, 313U },
};
static const we_menu_page_t mn_page_wifi = { "Wi-Fi", mn_wifi_items, 4U };

static const we_menu_item_t mn_language_items[] = {
    { "English",  NULL, 410U },
    { "Deutsch",  NULL, 411U },
    { "Francais", NULL, 412U },
    { "Espanol",  NULL, 413U },
};
static const we_menu_page_t mn_page_language = { "Language", mn_language_items, 4U };

/* --- 第二层（子页，部分行再带孙页） --- */
static const we_menu_item_t mn_display_items[] = {
    { "Brightness",    &mn_page_brightness, 0U },
    { "Night Mode",    NULL, 120U },
    { "Auto Rotate",   NULL, 121U },
    { "Sleep Timeout", &mn_page_timeout, 0U },
    { "Font Size",     NULL, 122U },
    { "Color Theme",   NULL, 123U },
};
static const we_menu_page_t mn_page_display = { "Display", mn_display_items, 6U };

static const we_menu_item_t mn_sound_items[] = {
    { "Volume",    &mn_page_volume, 0U },
    { "Ringtone",  NULL, 220U },
    { "Key Click", NULL, 221U },
    { "Alarm",     NULL, 222U },
    { "Vibration", NULL, 223U },
};
static const we_menu_page_t mn_page_sound = { "Sound", mn_sound_items, 5U };

static const we_menu_item_t mn_network_items[] = {
    { "Wi-Fi",         &mn_page_wifi, 0U },
    { "Bluetooth",     NULL, 320U },
    { "Hotspot",       NULL, 321U },
    { "VPN",           NULL, 322U },
    { "Proxy",         NULL, 323U },
    { "Airplane Mode", NULL, 324U },
};
static const we_menu_page_t mn_page_network = { "Network", mn_network_items, 6U };

static const we_menu_item_t mn_system_items[] = {
    { "Language",      &mn_page_language, 0U },
    { "Date & Time",   NULL, 420U },
    { "Storage",       NULL, 421U },
    { "Battery",       NULL, 422U },
    { "Updates",       NULL, 423U },
    { "Backup",        NULL, 424U },
    { "Factory Reset", NULL, 425U },
};
static const we_menu_page_t mn_page_system = { "System", mn_system_items, 7U };

/* --- 第一层（根页） --- */
static const we_menu_item_t mn_root_items[] = {
    { "Display", &mn_page_display, 0U },
    { "Sound",   &mn_page_sound, 0U },
    { "Network", &mn_page_network, 0U },
    { "System",  &mn_page_system, 0U },
    { "About",   NULL, 900U },
};
static const we_menu_page_t mn_page_root = { "Settings", mn_root_items, 5U };

/**
 * @brief 叶子行动作回调：把 "label (id)" 回显到顶部 label。
 * @param menu 传入：菜单控件对象指针（void * 透传，本 demo 未使用）
 * @param action_id 传入：被点行的动作 ID
 * @param item 传入：被点行条目指针
 * @return 无
 */
static void mn_on_action(void *menu, uint16_t action_id, const we_menu_item_t *item)
{
    (void)menu;
    if (item == NULL || item->label == NULL)
        return;
    sprintf(mn_sel_buf, "%s (%u)", item->label, (unsigned)action_id);
    we_label_set_text(&mn_sel_label, mn_sel_buf);
}

/**
 * @brief 初始化 menu demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_menu_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    mn_fps_timer = 0U;
    mn_last_frames = 0U;
    memset(mn_fps_buf, 0, sizeof(mn_fps_buf));
    memset(mn_sel_buf, 0, sizeof(mn_sel_buf));

    we_label_obj_init(&mn_title, lcd, 14, 10,
                      "MENU", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&mn_fps_label, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* 顶部回显：初始提示文案 */
    sprintf(mn_sel_buf, "> tap a leaf item");
    we_label_obj_init(&mn_sel_label, lcd, 14, 34,
                      mn_sel_buf, we_font_consolas_18,
                      RGB888TODEV(112, 184, 255), 255);

    /* 菜单面板：三层树，行区可滚动，右滑/返回箭头出栈 */
    we_menu_obj_init(&mn_menu, lcd, MN_PANEL_X, MN_PANEL_Y,
                     MN_PANEL_W, MN_PANEL_H, &mn_page_root, we_font_consolas_18);
    we_menu_set_action_cb(&mn_menu, mn_on_action);
}

/**
 * @brief menu demo 周期更新（仅刷新 FPS，菜单交互全部事件驱动）
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_menu_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &mn_fps_label, &mn_fps_timer,
                       &mn_last_frames, mn_fps_buf, ms_tick);
}
