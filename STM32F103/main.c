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
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "main.h"
#include "w25qxx.h"
#include "stm32f103_we_input_port.h"
#include "simple_widget_demos.h"

#define DEMO_SWITCH_MS_DEFAULT (5000UL)
#define DEMO_TICK_PERIOD_MS    (16U)

typedef void (*we_demo_init_fn_t)(we_lcd_t *lcd);
typedef void (*we_demo_tick_fn_t)(we_lcd_t *lcd, uint16_t elapsed_ms);

typedef struct
{
    we_demo_init_fn_t init;
    we_demo_tick_fn_t tick;
    uint32_t show_ms;
} we_demo_entry_t;

void delay_ms(uint32_t ms)
{
    g_sys_delay = ms;
    while (g_sys_delay)
    {
    }
}

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

static const we_demo_entry_t s_demo_table[] = {
		{ we_chart_simple_demo_init, we_chart_simple_demo_tick, 28000UL },
    { we_label_simple_demo_init, we_label_simple_demo_tick, DEMO_SWITCH_MS_DEFAULT },
    { we_btn_simple_demo_init, we_btn_simple_demo_tick, DEMO_SWITCH_MS_DEFAULT },
    { we_img_simple_demo_init, we_img_simple_demo_tick, DEMO_SWITCH_MS_DEFAULT },
    { we_img_ex_simple_demo_init, we_img_ex_simple_demo_tick, DEMO_SWITCH_MS_DEFAULT },
    { we_arc_simple_demo_init, we_arc_simple_demo_tick, DEMO_SWITCH_MS_DEFAULT },
    { we_group_simple_demo_init, we_group_simple_demo_tick, DEMO_SWITCH_MS_DEFAULT },
    { we_slideshow_simple_demo_init, we_slideshow_simple_demo_tick, 8000UL },
    { we_concentric_arc_simple_demo_init, we_concentric_arc_simple_demo_tick, DEMO_SWITCH_MS_DEFAULT },
    { we_key_simple_demo_init, we_key_simple_demo_tick, DEMO_SWITCH_MS_DEFAULT },
    { we_checkbox_simple_demo_init, we_checkbox_simple_demo_tick, DEMO_SWITCH_MS_DEFAULT },
    { we_label_ex_simple_demo_init, we_label_ex_simple_demo_tick, DEMO_SWITCH_MS_DEFAULT },
    { we_toggle_simple_demo_init, we_toggle_simple_demo_tick, DEMO_SWITCH_MS_DEFAULT },
    { we_progress_simple_demo_init, we_progress_simple_demo_tick, DEMO_SWITCH_MS_DEFAULT },
    { we_msgbox_simple_demo_init, we_msgbox_simple_demo_tick, 8000UL },
    { we_flash_img_simple_demo_init, we_flash_img_simple_demo_tick, 8000UL },
    { we_flash_font_simple_demo_init, we_flash_font_simple_demo_tick, 8000UL },
};

#define DEMO_COUNT ((uint8_t)(sizeof(s_demo_table) / sizeof(s_demo_table[0])))

static void demo_start(we_lcd_t *lcd, uint8_t demo_index)
{
    if (demo_index >= DEMO_COUNT)
        demo_index = 0U;

#if (LCD_DEEP == DEEP_RGB565)
    we_gui_init(lcd, RGB888TODEV(10, 14, 20), s_gram, USER_GRAM_NUM,
                lcd_set_addr, lcd_rgb565_port, we_input_port_read, w25qxx_read_data);
#elif (LCD_DEEP == DEEP_RGB888)
    we_gui_init(lcd, RGB888TODEV(10, 14, 20), s_gram, USER_GRAM_NUM,
                lcd_set_addr, lcd_rgb888_port, we_input_port_read, w25qxx_read_data);
#endif

    s_demo_table[demo_index].init(lcd);
    we_gui_timer_create(lcd, s_demo_table[demo_index].tick, DEMO_TICK_PERIOD_MS, 1U);
}

int main(void)
{
    uint8_t current_demo_idx = 0U;
    uint32_t demo_elapsed_ms = 0U;

    system_init();

    lcd_hw_init();
    lcd_bl_on();
    input_hw_init();
    flash_port_init();
    w25qxx_init();

    demo_start(&g_lcd, current_demo_idx);

    g_sys_tick = 0U;
    while (1)
    {
        if (g_sys_tick >= 1U)
        {
            uint16_t ms = g_sys_tick;
            g_sys_tick = 0U;

            we_gui_tick_inc(&g_lcd, ms);
            demo_elapsed_ms += ms;

            while (demo_elapsed_ms >= s_demo_table[current_demo_idx].show_ms)
            {
                demo_elapsed_ms -= s_demo_table[current_demo_idx].show_ms;
                current_demo_idx++;
                if (current_demo_idx >= DEMO_COUNT)
                    current_demo_idx = 0U;

                demo_start(&g_lcd, current_demo_idx);
            }
        }

        we_gui_task_handler(&g_lcd);
    }
}
