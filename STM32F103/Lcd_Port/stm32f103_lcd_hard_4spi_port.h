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

#ifndef STM32F103_LCD_HARD_4SPI_PORT_H
#define STM32F103_LCD_HARD_4SPI_PORT_H

#include "stdint.h"
#include "stm32f10x.h"
#include "stm32f10x_spi.h"

/* ---- SPI 外设 ---- */
#define LCD_SPIx                SPI1
#define RCC_APB2Periph_SPIx     RCC_APB2Periph_SPI1

/* ---- SPI 状态宏 (寄存器操作) ---- */
/* 等待发送缓冲器为空 */
#define wait_lcd_spi_txtemp_free() \
    do { while ((LCD_SPIx->SR & SPI_I2S_FLAG_TXE) == (uint16_t)RESET) {} } while (0)
/* 向发送缓冲器写入数据 */
#define send_lcd_spi_dat(dat) \
    do { LCD_SPIx->DR = (dat); } while (0)
/* 等待 SPI 总线空闲 (发送完成) */
#define send_lcd_spi_done() \
    do { while ((LCD_SPIx->SR & SPI_I2S_FLAG_BSY) != (uint16_t)RESET) {} } while (0)

/* ---- GPIO 宏 ---- */

/* SCL — PA5, SPI1_SCK (复用推挽) */
#define LCD_SCL_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_5; \
        gpio.GPIO_Speed = GPIO_Speed_50MHz; \
        gpio.GPIO_Mode  = GPIO_Mode_AF_PP; \
        GPIO_Init(GPIOA, &gpio); \
    } while (0)

/* SDA — PA7, SPI1_MOSI (复用推挽) */
#define LCD_SDA_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_7; \
        gpio.GPIO_Speed = GPIO_Speed_50MHz; \
        gpio.GPIO_Mode  = GPIO_Mode_AF_PP; \
        GPIO_Init(GPIOA, &gpio); \
    } while (0)

/* RES — PB0 */
#define LCD_RES_Clr()     do { GPIOB->BRR  = GPIO_Pin_0; } while (0)
#define LCD_RES_Set()     do { GPIOB->BSRR = GPIO_Pin_0; } while (0)
#define LCD_RES_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_0; \
        gpio.GPIO_Speed = GPIO_Speed_50MHz; \
        gpio.GPIO_Mode  = GPIO_Mode_Out_PP; \
        GPIO_Init(GPIOB, &gpio); \
        LCD_RES_Set(); \
    } while (0)

/* DC — PA3 */
#define LCD_DC_Clr()     do { GPIOA->BRR  = GPIO_Pin_3; } while (0)
#define LCD_DC_Set()     do { GPIOA->BSRR = GPIO_Pin_3; } while (0)
#define LCD_DC_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_3; \
        gpio.GPIO_Speed = GPIO_Speed_50MHz; \
        gpio.GPIO_Mode  = GPIO_Mode_Out_PP; \
        GPIO_Init(GPIOA, &gpio); \
        LCD_DC_Set(); \
    } while (0)

/* BL — PA2 (背光) */
#define LCD_BL_Clr()     do { GPIOA->BRR  = GPIO_Pin_2; } while (0)
#define LCD_BL_Set()     do { GPIOA->BSRR = GPIO_Pin_2; } while (0)
#define LCD_BL_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_2; \
        gpio.GPIO_Speed = GPIO_Speed_50MHz; \
        gpio.GPIO_Mode  = GPIO_Mode_Out_PP; \
        GPIO_Init(GPIOA, &gpio); \
        lcd_bl_on(); \
    } while (0)

/* CS — PA4 (可选) */
#define LCD_CS_Clr()     do { GPIOA->BRR  = GPIO_Pin_4; } while (0)
#define LCD_CS_Set()     do { GPIOA->BSRR = GPIO_Pin_4; } while (0)
#define LCD_CS_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_4; \
        gpio.GPIO_Speed = GPIO_Speed_50MHz; \
        gpio.GPIO_Mode  = GPIO_Mode_Out_PP; \
        GPIO_Init(GPIOA, &gpio); \
        LCD_CS_Set(); \
    } while (0)

/* ---- 公开接口 ---- */

void lcd_delay_ms(volatile uint32_t ms);
void lcd_bl_on(void);
void lcd_bl_off(void);
uint8_t lcd_is_busy(void);
void lcd_hw_init(void);
void lcd_send_1Cmd(uint8_t dat);
void lcd_send_nCmd(uint8_t *p, uint16_t num);
void lcd_send_1Dat(uint8_t dat);
void lcd_send_nDat(uint8_t *p, uint16_t num);
void lcd_rgb565_port(uint16_t *gram, uint32_t pix_size);
void lcd_rgb888_port(uint8_t *gram, uint32_t pix_size);

#endif
