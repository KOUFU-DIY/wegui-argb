#ifndef STM32F103_WE_PORT_H
#define STM32F103_WE_PORT_H

#include <stdint.h>

/* ------------------------ 芯片自定义 ------------------------ */
#define SCREEN_X_OFFSET (20)//屏幕X方向偏移
#define SCREEN_Y_OFFSET (0)//屏幕Y方向偏移

#define _ST7735   (1) //移植请参考
#define _ST7789V3 (2)
#define _ST7789VW (3)
#define _ST7796S  (4)
#define _GC9A01   (5)
#define LCD_IC _ST7789V3

#define _SOFT_3SPI (1)
#define _SOFT_4SPI (2)
#define _HARD_4SPI (3)
#define _DMA_4SPI  (4)
#define LCD_PORT _HARD_4SPI

#if (LCD_PORT == _SOFT_3SPI)
#include "stm32f103_lcd_soft_3spi_port.h"
#elif (LCD_PORT == _SOFT_4SPI)
#include "stm32f103_lcd_soft_4spi_port.h"
#elif (LCD_PORT == _HARD_4SPI)
#include "stm32f103_lcd_hard_4spi_port.h"
#elif (LCD_PORT == _DMA_4SPI)
#include "stm32f103_lcd_dma_4spi_port.h"
#elif (LCD_PORT == _SOFT_IIC)
#include "stm32f103_lcd_soft_iic_port.h"
#elif (LCD_PORT == _HARD_IIC)
#include "stm32f103_lcd_hard_iic_port.h"
#else
#error ("Not support LCD_PORT!")
#endif

#if ((LCD_PORT == _HARD_4SPI) || (LCD_PORT == _DMA_4SPI))
#define RCC_HCLK_Divx RCC_HCLK_Div1
#define LCD_SPI_BaudRatePrescaler_x SPI_BaudRatePrescaler_2
#endif

#define _FLASH_NO_PORT (0)
#define _F_SOFT_STDSPI (1)
#define _F_HARD_STDSPI (2)
#define _F_DMA_STDSPI (3)
#define FLASH_PORT _F_DMA_STDSPI

#if (FLASH_PORT == _F_SOFT_STDSPI)
#include "stm32f103_flash_soft_stdspi_port.h"
#elif (FLASH_PORT == _F_HARD_STDSPI)
#include "stm32f103_flash_hard_stdspi_port.h"
#elif (FLASH_PORT == _F_DMA_STDSPI)
#include "stm32f103_flash_dma_stdspi_port.h"
#endif

#define _FLASH_NO_IC (0)
#define _FLASH_W25Qxx (1)
#define FLASH_IC _FLASH_W25Qxx

#if (FLASH_IC == _FLASH_W25Qxx)
#include "w25qxx.h"
#define flash_ic_init() do{w25qxx_init();} while (0)
#define flash_read_addr_ndat(addr, p, len) do{w25qxx_read_data(addr, p, len);} while (0)
#endif

#define FLASH_SPI_BaudRatePrescaler_x SPI_BaudRatePrescaler_2

#if (LCD_IC == _ST7735)
#define lcd_ic_init() do{st7735_init();}while(0)
#define lcd_set_addr st7735_set_addr
#include "st7735.h"
		
#elif (LCD_IC == _ST7789V3)
#define lcd_ic_init()  do{st7789v3_init();} while (0)
#define lcd_set_addr st7789v3_set_addr
#include "st7789v3.h"

#elif (LCD_IC == _ST7789VW)
#define lcd_ic_init()  do{st7789vw_init();} while (0)
#define lcd_set_addr st7789vw_set_addr

#include "st7789vw.h"
#elif (LCD_IC == _ST7796S)
#define lcd_ic_init() do{st7796s_init();} while (0)
#define lcd_set_addr st7796s_set_addr

#include "st7796s.h"
#elif (LCD_IC == _GC9A01)
#define lcd_ic_init() do{gc9a01_init();} while (0)
#define lcd_set_addr gc9a01_set_addr
#include "gc9a01.h"

#else
#warning ("No screen ic init!")
#endif

static inline void we_lcd_set_addr_port(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_set_addr(x0, y0, x1, y1);
}

#if (LCD_DEEP == DEEP_RGB565)
#define LCD_FLUSH_PORT lcd_rgb565_port
#elif (LCD_DEEP == DEEP_RGB888)
#define LCD_FLUSH_PORT lcd_rgb888_port
#endif

#endif
