#include "stm32f0xx_it.h"

#if defined(LCD_PORT) && (LCD_PORT == _DMA_4SPI)
#include "Lcd_Port/stm32f030_lcd_dma_4spi_port.h"
#endif

extern volatile uint8_t  g_sys_tick;
extern volatile uint32_t g_sys_delay;

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    while (1)
    {
    }
}

void SVC_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
    if (g_sys_tick < 255U)
    {
        g_sys_tick++;
    }
    if (g_sys_delay > 0U)
    {
        g_sys_delay--;
    }
}

void DMA1_Channel4_5_IRQHandler(void)
{
#if defined(LCD_PORT) && (LCD_PORT == _DMA_4SPI)
    lcd_dma_irq_handler();
#endif
}

