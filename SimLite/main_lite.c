/**
 * @file  main_lite.c
 * @brief SimLite 轻量模拟器入口（fenster 三平台底座，无 SDL）
 *
 * 与硬件目标（STM32F103/F030/AD14N）完全同构：改下面的 DEMO_ID 宏并重编即可
 * 切换 demo（tcc 全量编译约 1 秒）。本文件同时是移植模板——只有
 * 初始化 / 编译期选 demo / 主循环三段，无任何调试设施。
 *
 * 开发调试入口（运行时选 demo、--shot 抓帧自检）在 SimLite/debug/，
 * 用 build_lite.ps1 -Dev（或 build_lite.sh --dev）单独构建，日常构建不编译。
 */

#include "lite_port.h"
#include "simple_widget_demos.h"
#include "preview_demos.h"

we_lcd_t mylcd;
colour_t user_gram[USER_GRAM_NUM];

/**
 * @brief 程序入口：初始化端口与 GUI、加载选定 demo、进入主循环
 * @return 0 正常退出（窗口关闭）
 */
int main(void)
{
    /* 演示选择：改 DEMO_ID 即可切换加载哪个 demo（编译期宏切换）。
     * 编号与 STM32F103/F030/AD14N 完全一致；0 为模拟器专属：
     * 0  = showcase (全控件汇总, 需把 we_user_config.h 分辨率调到 800x480)
     * 1  = label (标签)           2  = btn (按钮)
     * 3  = img (图片)             4  = img_ex (旋转缩放图)
     * 5  = arc (圆弧)             6  = group (控件组)
     * 7  = slideshow (幻灯片)     8  = concentric arc (同心圆弧)
     * 9  = checkbox (勾选框)      10 = label_ex (旋转缩放文字)
     * 11 = chart (实时波形)       12 = toggle (拨动开关)
     * 13 = progress (进度条)      14 = msgbox (消息框)
     * 15 = flash img (外挂图片)   16 = flash font (外挂字库)
     * 17 = slider (滑条)          18 = scroll_panel (滚动容器)
     * 19 = dropdown (下拉列表)    20 = stepper (数值步进)
     * 21 = indicator (指示灯)     22 = line (线段)
     * 23 = box (面板盒)           24 = gauge (仪表盘)
     * 25 = list (列表菜单)        26 = roller (滚轮选择)
     * 27 = marquee (滚动字条)     28 = toast (轻提示)
     * 29 = focus (聚焦导航)       30 = focus2 (聚焦编辑态)
     * 31 = img_alpha (A1/A2/A4/A8 透明位图)
     * 32 = imgbtn (图片按钮)      33 = segdisp (数码管)
     *
     * preview 孵化区（101 起，随时可能下架；2026-07 重排：
     * 常用 12 个在前、其余靠后，今后毕业迁出的编号留空洞不复用）：
     * 101 = keyboard (软键盘)     102 = textarea (输入框+弹层键盘)
     * 103 = menu (多级菜单)       104 = spectrum (频谱柱)
     * 105 = logview (滚动日志)    106 = calendar (日历)
     * 107 = table (表格)          108 = qrcode (二维码)
     * 109 = chart_bar (柱状图)    110 = scale (刻度尺)
     * 111 = joystick (虚拟摇杆)   112 = statusbar (状态栏)
     * 113 = ime_pinyin (拼音输入法) 114 = spinner (加载指示)
     * 115 = btnmatrix (按键矩阵)  116 = radio (单选组)
     * 117 = (已毕业→33)           118 = knob (旋钮)
     * 119 = colorwheel (色轮)     120 = (已毕业→32)
     * 121 = animimg (帧动画)      122 = canvas (自绘画布)
     * 123 = tabview (页签容器)    124 = hold_btn (长按确认)
     * 125 = mask_group (蒙版容器) 126 = mlabel (多行文本) */
#ifndef DEMO_ID /* build_lite.ps1 -Demo N 可从命令行覆盖（-DDEMO_ID=N） */
#define DEMO_ID (33)
#endif

    static we_gui_timer_t demo_timer; /* demo 周期定时器节点（调用方持有） */
    uint32_t last_tick;

    lcd_hw_init();
    input_hw_init();
    storage_hw_init();

    we_gui_init(&mylcd, RGB888TODEV(10, 14, 20), user_gram, USER_GRAM_NUM, lcd_set_addr, LCD_FLUSH_PORT,
                we_input_port_read, we_storage_port_read);

    /* 按 DEMO_ID 编译期选择并加载对应 demo（只编译进选中的那一个）。 */
#if (DEMO_ID == 0)
    /* showcase 按 800x480 布局，分辨率不足时编译期提示。 */
#if (SCREEN_WIDTH < 800) || (SCREEN_HEIGHT < 480)
#warning "demo_showcase 按 800x480 布局编写，请把 we_user_config.h 的 SCREEN_WIDTH/SCREEN_HEIGHT 调大"
#endif
    we_showcase_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_showcase_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 1)
    we_label_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_label_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 2)
    we_btn_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_btn_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 3)
    we_img_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_img_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 4)
    we_img_ex_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_img_ex_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 5)
    we_arc_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_arc_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 6)
    we_group_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_group_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 7)
    we_slideshow_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_slideshow_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 8)
    we_concentric_arc_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_concentric_arc_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 9)
    we_checkbox_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_checkbox_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 10)
    we_label_ex_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_label_ex_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 11)
    we_chart_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_chart_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 12)
    we_toggle_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_toggle_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 13)
    we_progress_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_progress_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 14)
    we_msgbox_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_msgbox_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 15)
    we_flash_img_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_flash_img_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 16)
    we_flash_font_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_flash_font_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 17)
    we_slider_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_slider_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 18)
    we_scroll_panel_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_scroll_panel_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 19)
    we_dropdown_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_dropdown_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 20)
    we_stepper_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_stepper_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 21)
    we_indicator_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_indicator_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 22)
    we_line_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_line_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 23)
    we_box_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_box_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 24)
    we_gauge_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_gauge_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 25)
    we_list_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_list_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 26)
    we_roller_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_roller_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 27)
    we_marquee_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_marquee_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 28)
    we_toast_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_toast_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 29)
    we_focus_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_focus_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 30)
    we_focus2_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_focus2_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 31)
    we_img_alpha_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_img_alpha_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 32)
    we_imgbtn_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_imgbtn_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 33)
    we_segdisp_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_segdisp_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 101)
    we_keyboard_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_keyboard_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 102)
    we_textarea_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_textarea_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 103)
    we_menu_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_menu_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 104)
    we_spectrum_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_spectrum_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 105)
    we_logview_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_logview_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 106)
    we_calendar_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_calendar_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 107)
    we_table_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_table_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 108)
    we_qrcode_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_qrcode_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 109)
    we_chart_bar_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_chart_bar_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 110)
    we_scale_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_scale_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 111)
    we_joystick_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_joystick_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 112)
    we_statusbar_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_statusbar_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 113)
    we_ime_pinyin_preview_demo_init(&mylcd);
    //we_gui_timer_create(&mylcd, &demo_timer, we_ime_pinyin_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 114)
    we_spinner_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_spinner_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 115)
    we_btnmatrix_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_btnmatrix_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 116)
    we_radio_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_radio_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 118)
    we_knob_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_knob_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 119)
    we_colorwheel_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_colorwheel_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 121)
    we_animimg_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_animimg_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 122)
    we_canvas_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_canvas_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 123)
    we_tabview_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_tabview_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 124)
    we_hold_btn_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_hold_btn_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 125)
    we_mask_group_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_mask_group_preview_demo_tick, 16U, 1U);
#elif (DEMO_ID == 126)
    we_mlabel_preview_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_mlabel_preview_demo_tick, 16U, 1U);
#else
    we_label_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, &demo_timer, we_label_simple_demo_tick, 16U, 1U);
#endif

    last_tick = lite_ticks_ms();

    while (lite_handle_events(&mylcd))
    {
        uint32_t now = lite_ticks_ms();
        uint32_t delta = now - last_tick;
        uint32_t frame_ms;

        if (delta > 0U)
        {
            uint16_t ms = (uint16_t)((delta > 100U) ? 16U : delta);

            we_gui_tick_inc(&mylcd, ms);
            last_tick = now;
        }

        we_gui_task_handler(&mylcd);
        lite_present();

        frame_ms = lite_ticks_ms() - now;
        if (frame_ms < 15U)
            lite_sleep_ms(15U - frame_ms); /* ~60fps，让出 CPU */
        else
            lite_sleep_ms(1U);
    }

    return 0;
}
