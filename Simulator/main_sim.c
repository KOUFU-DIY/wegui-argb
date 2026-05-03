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
    uint8_t demo_id;
    uint32_t last_tick;

    (void)argc;
    (void)argv;

    lcd_hw_init();
    input_hw_init();
    storage_hw_init();

    /* demo_id:
     * 1  = label
     * 2  = btn
     * 3  = img
     * 4  = img_ex
     * 5  = arc
     * 6  = group
     * 7  = slideshow
     * 8  = concentric arc
     * 9  = key
     * 10 = checkbox
     * 11 = label_ex
     * 12 = chart
     * 13 = toggle
     * 14 = progress
     * 15 = msgbox
     * 16 = flash img
     * 17 = flash font
     */
    demo_id = 14;

    we_gui_init(&mylcd, RGB888TODEV(10, 14, 20), user_gram, USER_GRAM_NUM, lcd_set_addr, LCD_FLUSH_PORT,
                we_input_port_read, we_storage_port_read);

    switch (demo_id)
    {
    case 1:
        we_label_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_label_simple_demo_tick, 16U, 1U);
        break;
    case 2:
        we_btn_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_btn_simple_demo_tick, 16U, 1U);
        break;
    case 3:
        we_img_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_img_simple_demo_tick, 16U, 1U);
        break;
    case 4:
        we_img_ex_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_img_ex_simple_demo_tick, 16U, 1U);
        break;
    case 5:
        we_arc_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_arc_simple_demo_tick, 16U, 1U);
        break;
    case 6:
        we_group_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_group_simple_demo_tick, 16U, 1U);
        break;
    case 7:
        we_slideshow_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_slideshow_simple_demo_tick, 16U, 1U);
        break;
    case 8:
        we_concentric_arc_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_concentric_arc_simple_demo_tick, 16U, 1U);
        break;
    case 9:
        we_key_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_key_simple_demo_tick, 16U, 1U);
        break;
    case 10:
        we_checkbox_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_checkbox_simple_demo_tick, 16U, 1U);
        break;
    case 11:
        we_label_ex_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_label_ex_simple_demo_tick, 16U, 1U);
        break;
    case 12:
        we_chart_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_chart_simple_demo_tick, 16U, 1U);
        break;
    case 13:
        we_toggle_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_toggle_simple_demo_tick, 16U, 1U);
        break;
    case 14:
        we_progress_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_progress_simple_demo_tick, 16U, 1U);
        break;
    case 15:
        we_msgbox_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_msgbox_simple_demo_tick, 16U, 1U);
        break;
    case 16:
        we_flash_img_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_flash_img_simple_demo_tick, 16U, 1U);
        break;
    case 17:
        we_flash_font_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_flash_font_simple_demo_tick, 16U, 1U);
        break;
    default:
        we_label_simple_demo_init(&mylcd);
        we_gui_timer_create(&mylcd, we_label_simple_demo_tick, 16U, 1U);
        break;
    }

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
