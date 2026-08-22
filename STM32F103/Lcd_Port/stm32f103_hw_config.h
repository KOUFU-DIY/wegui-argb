#ifndef STM32F103_WE_PORT_H
#define STM32F103_WE_PORT_H

#include <stdint.h>
#include "we_user_config.h" /* 先取 LCD_DEEP/DEEP_* 等用户配置：本头可能被端口 .c 直接包含 */

/* 屏幕类型 LCD_TYPE */
#define HW_LCD_OLED        (1)
#define HW_LCD_GRAY        (2)
#define HW_LCD_RGB565    (3)
#define HW_LCD_RGB888    (4)

/* ------------------------ 1.选择屏幕驱动芯片 ------------------------ */
/* 屏幕驱动芯片 LCD_IC  */
#define _ST7735      (1) //TFT移植请参考
#define _ST7789V3  (2)//TFT
#define _ST7789VW (3)//TFT
#define _ST7796S    (4)//TFT
#define _GC9A01     (5)//TFT
#define _SSD1306   (6)  //页式单色OLED 128x64 最常用(需 we_user_config.h 选 GRAY8 方案)
#define _SSD1309   (7)  //页式单色OLED 128x64 (2.42寸常见)
#define _SSD1312   (8)  //页式单色OLED 128x64
#define _SSD1315   (9)  //页式单色OLED 128x64 (SSD1306 兼容型)
#define _SH1106     (10) //页式单色OLED 128x64 (1.3寸常见, 列偏移2)
#define _SH1107     (11) //页式单色OLED 64x128/128x128
#define _SH1108     (12) //页式单色OLED 160x128 (SEG160)
#define _SH1115     (13) //页式单色OLED 128x64
#define _SSD1327   (14) //灰度OLED 128x128 16级灰度(需 GRAY8 方案 + ALIGN_X=2)
#define LCD_IC        _SSD1306

/* ------------------------ 2.选择屏幕类型(自动) ------------------------ */
#if (LCD_IC == _SSD1306) || (LCD_IC == _SSD1309) || (LCD_IC == _SSD1312) || (LCD_IC == _SSD1315) ||(LCD_IC == _SH1106) || (LCD_IC == _SH1107) || (LCD_IC == _SH1108) || (LCD_IC == _SH1115)
	#define LCD_TYPE HW_LCD_OLED
#elif (LCD_IC == _SSD1327)
	#define LCD_TYPE HW_LCD_GRAY
#else
	#define LCD_TYPE HW_LCD_RGB565
	//#define LCD_TYPE HW_LCD_RGB888
#endif

/* ------------------------ 2.1设置灰度转单色阈值 ------------------------ */
/* 单色 OLED：灰度 -> 单色阈值（0~255） */
#if (LCD_TYPE == HW_LCD_OLED)
	#define WE_OLED_GRAY_THRESHOLD (128U)
#endif

/* ------------------------ 3.1选择驱动接口 ------------------------ */
/* 驱动接口 LCD_PORT */
#define _SOFT_3SPI  (1)
#define _SOFT_4SPI  (2)
#define _HARD_4SPI (3)
#define _DMA_4SPI  (4)
#define _SOFT_IIC    (5)
#define LCD_PORT _SOFT_IIC

/* ------------------------ 3.2设置SPI接口速率 ------------------------ */
/* 注意：RCC_HCLK_Divx 是全局 APB2 分频（影响 GPIO/USART1/ADC 等所有
 * APB2 外设，不只 LCD SPI）；仅 LCD 端口 init 会应用它，Flash 端口
 * 挂 APB1 不涉及。 */
#if ((LCD_PORT == _HARD_4SPI) || (LCD_PORT == _DMA_4SPI))
	#define RCC_HCLK_Divx RCC_HCLK_Div1
	#define LCD_SPI_BaudRatePrescaler_x SPI_BaudRatePrescaler_2
#endif

/* ------------------------ 3.3设置I2C接口 ------------------------ */
#if (LCD_PORT == _SOFT_IIC)
	#define OLED_IIC_7ADDR (0x3C) //7位0x3C => 8位0x78 (大部分默认)
	// #define OLED_IIC_7ADDR (0x3D) //7位0x3D => 8位0x7A
#endif

/* ------------------------ 4.设置偏移显示 ------------------------ */
#define SCREEN_X_OFFSET (0)//屏幕X方向偏移 默认0
#define SCREEN_Y_OFFSET (0)//屏幕Y方向偏移 默认0

/* ------------------------ 5.设置外挂储存接口 ------------------------ */
#define _FLASH_NO_PORT (0)
#define _F_SOFT_STDSPI (1)
#define _F_HARD_STDSPI (2)
#define _F_DMA_STDSPI (3)
#define FLASH_PORT _F_DMA_STDSPI

/* ------------------------ 5.设置外挂Flash速度 ------------------------ */
#define FLASH_SPI_BaudRatePrescaler_x SPI_BaudRatePrescaler_2

/* ------------------------ 5.1设置外挂flash芯片 ------------------------ */
#define _FLASH_NO_IC (0)
#define _FLASH_W25Qxx (1)
#define FLASH_IC _FLASH_W25Qxx

/* ------------------------ 6.参数检查(自动) ------------------------ */
/* OLED/灰度 IC 只能配 GRAY8 色深 */
#if ((LCD_TYPE == HW_LCD_OLED) || (LCD_TYPE == HW_LCD_GRAY)) && (LCD_DEEP != DEEP_GRAY8)
#error "OLED/GRAY LCD_IC requires LCD_DEEP == DEEP_GRAY8: set LCD_DEEP to DEEP_GRAY8 in we_user_config.h"
#endif
/* OLED 必须y8行刷新(脏矩形对齐) */
#if ((LCD_TYPE == HW_LCD_OLED) && (WE_LCD_FLUSH_ALIGN_Y != 8))
#error "set WE_LCD_FLUSH_ALIGN_Y to 8 in we_user_config.h"
#endif
/* 灰度屏必须x或y两行刷新(脏矩形对齐) */
#if (LCD_TYPE == HW_LCD_GRAY) && (((WE_LCD_FLUSH_ALIGN_X % 2) !=0)||((WE_LCD_FLUSH_ALIGN_Y % 2) !=0))
#error "SSD1327 packs 2 pixels per byte: set WE_LCD_FLUSH_ALIGN_X or Y to 2 in we_user_config.h"
#endif
#if (LCD_DEEP == DEEP_GRAY8) && (LCD_TYPE == HW_LCD_RGB565)
#error "DEEP_GRAY8 requires an OLED/GRAY LCD_IC (SSD13xx/SH11xx family) on this target"
#endif
#if (LCD_PORT == _SOFT_IIC) && (LCD_TYPE != HW_LCD_OLED) && (LCD_TYPE != HW_LCD_GRAY)
#error "TFT does not support I2C driver: choose an SPI LCD_PORT"
#endif
/* 3 线 SPI 端口当前只带 TFT 刷屏（缺 OLED/GRAY 三件套），先编译期拦截 */
#if (LCD_PORT == _SOFT_3SPI) && (LCD_TYPE != HW_LCD_RGB565) && (LCD_TYPE != HW_LCD_RGB888)
#error "_SOFT_3SPI port has no OLED/GRAY flush support yet: use _SOFT_4SPI/_HARD_4SPI/_DMA_4SPI/_SOFT_IIC"
#endif

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
#else
#error ("Not support LCD_PORT!")
#endif

#if (FLASH_PORT == _F_SOFT_STDSPI)
#error "soft stdspi flash port not implemented yet: use _F_HARD_STDSPI or _F_DMA_STDSPI"
#elif (FLASH_PORT == _F_HARD_STDSPI)
#include "stm32f103_flash_hard_stdspi_port.h"
#elif (FLASH_PORT == _F_DMA_STDSPI)
#include "stm32f103_flash_dma_stdspi_port.h"
#endif

#if (FLASH_IC == _FLASH_W25Qxx)
#include "w25qxx.h"
#endif

#if (LCD_IC == _ST7735)
#define lcd_ic_init() do{st7735_init();}while(0)
#define lcd_set_windows st7735_set_addr
#include "st7735.h"
		
#elif (LCD_IC == _ST7789V3)
#define lcd_ic_init()  do{st7789v3_init();} while (0)
#define lcd_set_windows st7789v3_set_addr
#include "st7789v3.h"

#elif (LCD_IC == _ST7789VW)
#define lcd_ic_init()  do{st7789vw_init();} while (0)
#define lcd_set_windows st7789vw_set_addr

#include "st7789vw.h"
#elif (LCD_IC == _ST7796S)
#define lcd_ic_init() do{st7796s_init();} while (0)
#define lcd_set_windows st7796s_set_addr

#include "st7796s.h"
#elif (LCD_IC == _GC9A01)
#define lcd_ic_init() do{gc9a01_init();} while (0)
#define lcd_set_windows gc9a01_set_addr
#include "gc9a01.h"

#elif (LCD_IC == _SSD1306)
#define lcd_ic_init() do{ssd1306_init();} while (0)
#define oled_set_addr ssd1306_set_addr
#include "ssd1306.h"

#elif (LCD_IC == _SSD1309)
#define lcd_ic_init() do{ssd1309_init();} while (0)
#define oled_set_addr ssd1309_set_addr
#include "ssd1309.h"

#elif (LCD_IC == _SSD1312)
#define lcd_ic_init() do{ssd1312_init();} while (0)
#define oled_set_addr ssd1312_set_addr
#include "ssd1312.h"

#elif (LCD_IC == _SSD1315)
#define lcd_ic_init() do{ssd1315_init();} while (0)
#define oled_set_addr ssd1315_set_addr
#include "ssd1315.h"

#elif (LCD_IC == _SH1106)
#define lcd_ic_init() do{sh1106_init();} while (0)
#define oled_set_addr sh1106_set_addr
#include "sh1106.h"

#elif (LCD_IC == _SH1107)
#define lcd_ic_init() do{sh1107_init();} while (0)
#define oled_set_addr sh1107_set_addr
#include "sh1107.h"

#elif (LCD_IC == _SH1108)
#define lcd_ic_init() do{sh1108_init();} while (0)
#define oled_set_addr sh1108_set_addr
#include "sh1108.h"

#elif (LCD_IC == _SH1115)
#define lcd_ic_init() do{sh1115_init();} while (0)
#define oled_set_addr sh1115_set_addr
#include "sh1115.h"

#elif (LCD_IC == _SSD1327)
#define lcd_ic_init() do{ssd1327_init();} while (0)
#define gray_set_addr ssd1327_set_addr /* lcd_set_windows 实体在端口层，经此宏转发硬件窗口命令 */
#include "ssd1327.h"

#else
#warning ("No screen ic init!")
#endif

/* 端口刷屏是否真异步（DMA 后台发送、flush 立即返回）。
 * 只有真异步端口才值得启用 GRAM 双缓冲（GRAM_DMA_BUFF_EN）；
 * 阻塞端口下双缓冲只会把可用 PFB 白白砍半、flush 次数翻倍。 */
#define WE_PORT_FLUSH_ASYNC ((LCD_PORT == _DMA_4SPI) ? 1 : 0)

#endif
