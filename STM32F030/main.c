#include "main.h"
#include "stm32f030_we_input_port.h"
#include "simple_widget_demos.h"
#include "we_gui_driver.h"

volatile uint8_t  g_sys_tick;
volatile uint32_t g_sys_delay;

void delay_ms(uint32_t ms)
{
    g_sys_delay = ms;
    while (g_sys_delay)
    {
    }
}

static void system_init(void)
{
    g_sys_tick = 0;
		FLASH_PrefetchBufferCmd(ENABLE);
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);
}

static colour_t s_gram[USER_GRAM_NUM];
we_lcd_t g_lcd;

int main(void)
{
    uint8_t demo_id;

    system_init();
		 delay_ms(1000U);
    lcd_hw_init();
    lcd_bl_on();
    input_hw_init();

#if (LCD_DEEP == DEEP_RGB565)
    we_gui_init(&g_lcd, RGB888TODEV(10, 14, 20), s_gram, USER_GRAM_NUM,
                lcd_set_addr, lcd_rgb565_port, we_input_port_read, NULL);
#elif (LCD_DEEP == DEEP_RGB888)
    we_gui_init(&g_lcd, RGB888TODEV(10, 14, 20), s_gram, USER_GRAM_NUM,
                lcd_set_addr, lcd_rgb888_port, we_input_port_read, NULL);
#endif

    /* demo_id:
     * 1  = label
     * 2  = btn
     * 3  = img
     * 4  = img_ex
     * 5  = arc
     * 6  = group
     * 7  = slideshow
     * 8  = concentric arc
     * 9  = checkbox
     * 10 = label_ex
     * 11 = chart
     * 12 = toggle
     * 13 = progress
     * 14 = msgbox
     * 15 = flash img
     * 16 = flash font
     * 17 = slider
     * 18 = scroll_panel
     */
    demo_id = 17;

    switch (demo_id)
    {
    case 1:
        we_label_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_label_simple_demo_tick, 16U, 1U);
        break;
    case 2:
        we_btn_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_btn_simple_demo_tick, 16U, 1U);
        break;
    case 3:
        we_img_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_img_simple_demo_tick, 16U, 1U);
        break;
    case 4:
        we_img_ex_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_img_ex_simple_demo_tick, 16U, 1U);
        break;
    case 5:
        we_arc_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_arc_simple_demo_tick, 16U, 1U);
        break;
    case 6:
        we_group_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_group_simple_demo_tick, 16U, 1U);
        break;
    case 7:
        we_slideshow_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_slideshow_simple_demo_tick, 16U, 1U);
        break;
    case 8:
        we_concentric_arc_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_concentric_arc_simple_demo_tick, 16U, 1U);
        break;
    case 9:
        we_checkbox_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_checkbox_simple_demo_tick, 16U, 1U);
        break;
    case 10:
        we_label_ex_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_label_ex_simple_demo_tick, 16U, 1U);
        break;
    case 11:
        we_chart_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_chart_simple_demo_tick, 16U, 1U);
        break;
    case 12:
        we_toggle_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_toggle_simple_demo_tick, 16U, 1U);
        break;
    case 13:
        we_progress_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_progress_simple_demo_tick, 16U, 1U);
        break;
    case 14:
        we_msgbox_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_msgbox_simple_demo_tick, 16U, 1U);
        break;
    case 15:
        we_flash_img_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_flash_img_simple_demo_tick, 16U, 1U);
        break;
    case 16:
        we_flash_font_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_flash_font_simple_demo_tick, 16U, 1U);
        break;
    case 17:
        we_slider_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_slider_simple_demo_tick, 16U, 1U);
        break;
    case 18:
        we_scroll_panel_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_scroll_panel_simple_demo_tick, 16U, 1U);
        break;
    default:
        we_btn_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_btn_simple_demo_tick, 16U, 1U);
        break;
    }

    g_sys_tick = 0;
    while (1)
    {
        if (g_sys_tick >= 1U)
        {
            uint16_t ms = g_sys_tick;
            g_sys_tick = 0;
            we_gui_tick_inc(&g_lcd, ms);
        }
        we_gui_task_handler(&g_lcd);
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line)
{
    (void)file;
    (void)line;
    while (1)
    {
    }
}
#endif
