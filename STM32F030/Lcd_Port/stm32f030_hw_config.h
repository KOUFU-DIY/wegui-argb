#ifndef STM32F030_HW_CONFIG_H
#define STM32F030_HW_CONFIG_H

#include <stdint.h>
#include "we_user_config.h" /* 先取 LCD_DEEP/DEEP_* 等用户配置：本头可能被端口 .c 直接包含 */

/* ---- 参数检查：本目标暂只有 TFT SPI 端口 ----
 * F030 尚无 OLED/GRAY 端口与屏 IC 驱动，GRAY8 色深编译期直接拦截——
 * 否则 main.c 的 we_gui_init 链一个分支都不命中，编译通过但上电黑屏。
 * 需要单色/灰度屏请用 STM32F103 目标（14 款 IC + 软IIC/SPI 端口齐全），
 * 或照 F103 的 Lcd_Port 结构给本目标补 OLED/GRAY 刷屏三件套。 */
#if (LCD_DEEP == DEEP_GRAY8)
#error "STM32F030 target has no OLED/GRAY port yet: DEEP_GRAY8 unsupported here (use STM32F103 target, or port the stm32f103 oled/gray Lcd_Port files)"
#endif

#define SCREEN_X_OFFSET (0) /* 240 宽面板与 240 宽 GUI 等宽，无需裁剪偏移 */
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

/* 端口刷屏是否异步（DMA 后台发送、flush 立即返回），填入 main.c 端口描述的 flush_async。
 * 只有 DMA 端口的 RGB565 刷屏在 GRAM_DMA_BUFF_EN = 1 时不等发送完成就返回
 * （内核随之把显存分两半交替使用）；其余刷屏函数都是发完才返回。 */
#define LCD_FLUSH_ASYNC (((LCD_PORT == _DMA_4SPI) && (GRAM_DMA_BUFF_EN == 1) && (LCD_DEEP == DEEP_RGB565)) ? 1U : 0U)

#if (LCD_IC == _ST7735)
#include "st7735.h"
#define lcd_ic_init() do{st7735_init();}while(0)
#define lcd_set_windows st7735_set_addr

#elif (LCD_IC == _ST7789V3)
#include "st7789v3.h"
#define lcd_ic_init() do{st7789v3_init();}while(0)
#define lcd_set_windows st7789v3_set_addr

#elif (LCD_IC == _ST7789VW)
#include "st7789vw.h"
#define lcd_ic_init() do{st7789vw_init();}while(0)
#define lcd_set_windows st7789vw_set_addr

#elif (LCD_IC == _ST7789S)
#include "st7789s.h"
#define lcd_ic_init() do{st7789s_init();}while(0)
#define lcd_set_windows st7789s_set_addr

#elif (LCD_IC == _ST7796S)
#include "st7796s.h"
#define lcd_ic_init() do{st7796s_init();}while(0)
#define lcd_set_windows st7796s_set_addr

#elif (LCD_IC == _GC9A01)
#include "gc9a01.h"
#define lcd_ic_init() do{gc9a01_init();}while(0)
#define lcd_set_windows gc9a01_set_addr

#else
#warning ("No screen ic init!")
#endif



#endif
