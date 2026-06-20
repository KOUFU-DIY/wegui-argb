#include "sdl_port.h"
#include "simple_widget_demos.h"
#include "we_gui_driver.h"
#include <SDL.h>

we_lcd_t mylcd;
colour_t user_gram[USER_GRAM_NUM];

/**
 * @brief 模拟器程序入口，初始化平台并运行选定演示
 * @param argc 命令行参数数量（未使用）
 * @param argv 命令行参数数组（未使用）
 * @return 进程退出码，0 表示正常退出
 */
int main(int argc, char *argv[])
{
    /* 演示选择：改 DEMO_ID 即可切换加载哪个 demo（编译期宏切换）。
     * 编号与 STM32F103/F030 的 1..21 完全一致，0 为模拟器专属：
     * 0  = showcase (全控件汇总，仅模拟器，需 800x480)
     * 1  = label          2  = btn            3  = img
     * 4  = img_ex         5  = arc            6  = group
     * 7  = slideshow      8  = concentric arc 9  = checkbox
     * 10 = label_ex       11 = chart          12 = toggle
     * 13 = progress       14 = msgbox         15 = flash img
     * 16 = flash font     17 = slider         18 = scroll_panel
     * 19 = dropdown       20 = stepper        21 = indicator */
#define DEMO_ID (0)

    uint32_t last_tick;

    (void)argc;
    (void)argv;

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
    we_gui_timer_create(&mylcd, we_showcase_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 1)
    we_label_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_label_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 2)
    we_btn_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_btn_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 3)
    we_img_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_img_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 4)
    we_img_ex_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_img_ex_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 5)
    we_arc_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_arc_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 6)
    we_group_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_group_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 7)
    we_slideshow_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_slideshow_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 8)
    we_concentric_arc_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_concentric_arc_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 9)
    we_checkbox_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_checkbox_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 10)
    we_label_ex_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_label_ex_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 11)
    we_chart_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_chart_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 12)
    we_toggle_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_toggle_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 13)
    we_progress_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_progress_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 14)
    we_msgbox_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_msgbox_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 15)
    we_flash_img_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_flash_img_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 16)
    we_flash_font_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_flash_font_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 17)
    we_slider_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_slider_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 18)
    we_scroll_panel_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_scroll_panel_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 19)
    we_dropdown_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_dropdown_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 20)
    we_stepper_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_stepper_simple_demo_tick, 16U, 1U);
#elif (DEMO_ID == 21)
    we_indicator_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_indicator_simple_demo_tick, 16U, 1U);
#else
    we_label_simple_demo_init(&mylcd);
    we_gui_timer_create(&mylcd, we_label_simple_demo_tick, 16U, 1U);
#endif

    last_tick = SDL_GetTicks();

    while (sim_handle_events(&mylcd))
    {
        uint32_t current_tick = SDL_GetTicks();
        uint32_t delta = current_tick - last_tick;

        if (delta > 0U)
        {
            uint16_t ms = (uint16_t)((delta > 100U) ? 16U : delta);
            we_gui_tick_inc(&mylcd, ms);
            last_tick = current_tick;
        }

        we_gui_task_handler(&mylcd);
        sim_lcd_update();
    }

    SDL_Quit();
    return 0;
}
