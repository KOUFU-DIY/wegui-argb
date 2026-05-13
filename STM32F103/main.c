/*
 * Copyright 2025 Lu Zhihao
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissionas and
 * limitations under the License.
 */

#include "main.h"
#include "w25qxx.h"
#include "stm32f103_we_input_port.h"
#include "simple_widget_demos.h"

/**
 * @brief ���뼶������ʱ
 * @param ms ��ʱʱ��, ��λ����
 */
void delay_ms(uint32_t ms)
{
    g_sys_delay = ms;
    while (g_sys_delay)
    {
    }
}

/**
 * @brief ��ϵͳʱ���л��� HSI/2 �� PLL ��Ƶ��� 64MHz
 */
static void hsi_set_sysclk_64(void)
{
    RCC_DeInit();
    RCC_HSICmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET)
    {
    }

    FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);
    FLASH_SetLatency(FLASH_Latency_2);
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK2Config(RCC_HCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div2);

    RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_16);
    RCC_PLLCmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET)
    {
    }

    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    while (RCC_GetSYSCLKSource() != 0x08)
    {
    }
}

/**
 * @brief ���ϵͳʱ�Ӳ����쳣ʱִ��ʱ�Ӿ�Ԯ
 */
static void sysclk_check_rescue(void)
{
    RCC_ClocksTypeDef clocks;
    RCC_GetClocksFreq(&clocks);

    if (clocks.SYSCLK_Frequency < 72000000U)
    {
        hsi_set_sysclk_64();
        SystemCoreClockUpdate();
    }
}

/**
 * @brief ϵͳ��ʼ��
 * @details ִ��ʱ�Ӽ�顢���Զ˿���ӳ���� SysTick 1ms ��������
 */
static void system_init(void)
{
    sysclk_check_rescue();
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
    g_sys_tick = 0;
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);
}

static colour_t s_gram[USER_GRAM_NUM];
we_lcd_t g_lcd;

int main(void)
{
    uint8_t demo_id;

    system_init();

    lcd_hw_init();
    lcd_bl_on();
    input_hw_init();
    flash_port_init();
    w25qxx_init();

#if (LCD_DEEP == DEEP_RGB565)
    we_gui_init(&g_lcd, RGB888TODEV(10, 14, 20), s_gram, USER_GRAM_NUM,
                lcd_set_addr, lcd_rgb565_port, we_input_port_read, w25qxx_read_data);
#elif (LCD_DEEP == DEEP_RGB888)
    we_gui_init(&g_lcd, RGB888TODEV(10, 14, 20), s_gram, USER_GRAM_NUM,
                lcd_set_addr, lcd_rgb888_port, we_input_port_read, w25qxx_read_data);
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
     * 9  = key
     * 10 = checkbox
     * 11 = label_ex
     * 12 = chart
     * 13 = toggle
     * 14 = progress
     * 15 = msgbox
     * 16 = flash img
     * 17 = flash font
     * 18 = slider
     * 19 = scroll_panel
     */
    demo_id = 18;

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
        we_key_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_key_simple_demo_tick, 16U, 1U);
        break;
    case 10:
        we_checkbox_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_checkbox_simple_demo_tick, 16U, 1U);
        break;
    case 11:
        we_label_ex_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_label_ex_simple_demo_tick, 16U, 1U);
        break;
    case 12:
        we_chart_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_chart_simple_demo_tick, 16U, 1U);
        break;
    case 13:
        we_toggle_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_toggle_simple_demo_tick, 16U, 1U);
        break;
    case 14:
        we_progress_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_progress_simple_demo_tick, 16U, 1U);
        break;
    case 15:
        we_msgbox_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_msgbox_simple_demo_tick, 16U, 1U);
        break;
    case 16:
        we_flash_img_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_flash_img_simple_demo_tick, 16U, 1U);
        break;
    case 17:
        we_flash_font_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_flash_font_simple_demo_tick, 16U, 1U);
        break;
    case 18:
        we_slider_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_slider_simple_demo_tick, 16U, 1U);
        break;
    case 19:
        we_scroll_panel_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_scroll_panel_simple_demo_tick, 16U, 1U);
        break;
    default:
        we_key_simple_demo_init(&g_lcd);
        we_gui_timer_create(&g_lcd, we_key_simple_demo_tick, 16U, 1U);
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
