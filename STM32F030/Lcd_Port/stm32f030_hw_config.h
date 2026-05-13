#ifndef STM32F030_HW_CONFIG_H
#define STM32F030_HW_CONFIG_H

#include <stdint.h>

#define SCREEN_X_OFFSET (0)
#define SCREEN_Y_OFFSET (0)

#define _ST7735   (1)
#define _ST7789V3 (2)
#define _ST7789VW (3)
#define _ST7789S  (4)
#define _ST7796S  (5)
#define _GC9A01   (6)
#define LCD_IC _ST7789V3

#define _SOFT_4SPI (2)
#define _HARD_4SPI (3)
#define _DMA_4SPI  (4)
#define LCD_PORT _DMA_4SPI

#if (LCD_PORT == _SOFT_4SPI)
#include "stm32f030_lcd_soft_4spi_port.h"
#elif (LCD_PORT == _HARD_4SPI)
#include "stm32f030_lcd_hard_4spi_port.h"
#elif (LCD_PORT == _DMA_4SPI)
#include "stm32f030_lcd_dma_4spi_port.h"
#else
#error ("Not support LCD_PORT!")
#endif

#if (LCD_IC == _ST7735)
#include "st7735.h"
#define lcd_ic_init() do{st7735_init();}while(0)
#define lcd_set_addr st7735_set_addr

#elif (LCD_IC == _ST7789V3)
#include "st7789v3.h"
#define lcd_ic_init() do{st7789v3_init();}while(0)
#define lcd_set_addr st7789v3_set_addr

#elif (LCD_IC == _ST7789VW)
#include "st7789vw.h"
#define lcd_ic_init() do{st7789vw_init();}while(0)
#define lcd_set_addr st7789vw_set_addr

#elif (LCD_IC == _ST7789S)
#include "st7789s.h"
#define lcd_ic_init() do{st7789s_init();}while(0)
#define lcd_set_addr st7789s_set_addr

#elif (LCD_IC == _ST7796S)
#include "st7796s.h"
#define lcd_ic_init() do{st7796s_init();}while(0)
#define lcd_set_addr st7796s_set_addr

#elif (LCD_IC == _GC9A01)
#include "gc9a01.h"
#define lcd_ic_init() do{gc9a01_init();}while(0)
#define lcd_set_addr gc9a01_set_addr

#else
#warning ("No screen ic init!")
#endif


#if (LCD_DEEP == DEEP_RGB565)
#define LCD_FLUSH_PORT lcd_rgb565_port
#elif (LCD_DEEP == DEEP_RGB888)
#define LCD_FLUSH_PORT lcd_rgb888_port
#endif

#endif
