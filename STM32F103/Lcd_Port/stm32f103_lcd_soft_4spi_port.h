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

#ifndef STM32F103_LCD_SOFT_4SPI_PORT_H
#define STM32F103_LCD_SOFT_4SPI_PORT_H

#include "stdint.h"
#include "stm32f10x.h"

/* ---- GPIO 宏 ---- */

/* SCL — PB13 */
#define LCD_SCL_Clr()     do { GPIOB->BRR  = GPIO_Pin_13; } while (0)
#define LCD_SCL_Set()     do { GPIOB->BSRR = GPIO_Pin_13; } while (0)
#define LCD_SCL_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_13; \
        gpio.GPIO_Speed = GPIO_Speed_50MHz; \
        gpio.GPIO_Mode  = GPIO_Mode_Out_PP; \
        GPIO_Init(GPIOB, &gpio); \
        LCD_SCL_Set(); \
    } while (0)

/* SDA — PB15 */
#define LCD_SDA_Clr()     do { GPIOB->BRR  = GPIO_Pin_15; } while (0)
#define LCD_SDA_Set()     do { GPIOB->BSRR = GPIO_Pin_15; } while (0)
#define LCD_SDA_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_15; \
        gpio.GPIO_Speed = GPIO_Speed_50MHz; \
        gpio.GPIO_Mode  = GPIO_Mode_Out_PP; \
        GPIO_Init(GPIOB, &gpio); \
        LCD_SDA_Set(); \
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

/* BL — PA2 */
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

/* CS — PA4 */
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
