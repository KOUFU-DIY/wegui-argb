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

#ifndef STM32F030_LCD_DMA_4SPI_PORT_H
#define STM32F030_LCD_DMA_4SPI_PORT_H

#include "stm32f0xx.h"
#include "stm32f0xx_dma.h"
#include "stm32f0xx_spi.h"
#include "stm32f0xx_gpio.h"
#include "stdint.h"

/* ---- SPI / DMA 外设 ---- */
#define LCD_SPIx                    SPI2
#define RCC_APB1Periph_SPIx         RCC_APB1Periph_SPI2
#define LCD_DMAx                    DMA1
#define LCD_DMA_CHANNELx            DMA1_Channel5
#define LCD_DMA_COMPLETE_FLAG       DMA1_FLAG_TC5
#define LCD_DMA_COMPLETE_IT         DMA1_IT_TC5
#define LCD_DMA_REMAP_REQ           DMA1_CH5_SPI2_TX
#define LCD_DMA_PeripheralBaseAddr  (&SPI2->DR)

/* ---- SPI / DMA 状态宏 (寄存器操作) ---- */
#define wait_lcd_dma_free() \
    do { while (lcd_is_busy()) {} } while (0)
#define wait_lcd_spi_txtemp_free() \
    do { while ((LCD_SPIx->SR & SPI_I2S_FLAG_TXE) == RESET) /*(SPI_I2S_GetFlagStatus(LCD_SPIx, SPI_I2S_FLAG_TXE) == RESET) */{} } while (0)
#define send_lcd_spi_dat8(dat) \
    do { *(__IO uint8_t *)(&LCD_SPIx->DR) = (uint8_t)(dat); } while (0)
#define send_lcd_spi_dat16(dat) \
    do { LCD_SPIx->DR = (uint16_t)(dat); } while (0)
#define send_lcd_spi_done() \
    do { while ((LCD_SPIx->SR & SPI_I2S_FLAG_BSY) != RESET) /* while (SPI_I2S_GetFlagStatus(LCD_SPIx, SPI_I2S_FLAG_BSY) != RESET) */{} } while (0)

/* ---- GPIO 宏 ---- */

/* SCL — PB13, SPI2_SCK (复用推挽) */
#define LCD_SCL_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_13; \
        gpio.GPIO_Mode  = GPIO_Mode_AF; \
        gpio.GPIO_OType = GPIO_OType_PP; \
        gpio.GPIO_Speed = GPIO_Speed_Level_3; \
        gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL; \
        GPIO_Init(GPIOB, &gpio); \
        GPIO_PinAFConfig(GPIOB, GPIO_PinSource13, GPIO_AF_0); \
    } while (0)

/* SDA — PB15, SPI2_MOSI (复用推挽) */
#define LCD_SDA_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_15; \
        gpio.GPIO_Mode  = GPIO_Mode_AF; \
        gpio.GPIO_OType = GPIO_OType_PP; \
        gpio.GPIO_Speed = GPIO_Speed_Level_3; \
        gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL; \
        GPIO_Init(GPIOB, &gpio); \
        GPIO_PinAFConfig(GPIOB, GPIO_PinSource15, GPIO_AF_0); \
    } while (0)

/* RES — PB14 */
#define LCD_RES_Clr()    do{GPIOB->BRR = GPIO_Pin_14;}while(0)
#define LCD_RES_Set()    do{GPIOB->BSRR = GPIO_Pin_14;}while(0)
#define LCD_RES_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_14; \
        gpio.GPIO_Mode  = GPIO_Mode_OUT; \
        gpio.GPIO_OType = GPIO_OType_PP; \
        gpio.GPIO_Speed = GPIO_Speed_Level_3; \
        gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL; \
        GPIO_Init(GPIOB, &gpio); \
        LCD_RES_Set(); \
    } while (0)

/* DC — PA12 */
#define LCD_DC_Clr()     do{GPIOA->BRR = GPIO_Pin_12;}while(0)
#define LCD_DC_Set()     do{GPIOA->BSRR = GPIO_Pin_12;}while(0)
#define LCD_DC_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_12; \
        gpio.GPIO_Mode  = GPIO_Mode_OUT; \
        gpio.GPIO_OType = GPIO_OType_PP; \
        gpio.GPIO_Speed = GPIO_Speed_Level_3; \
        gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL; \
        GPIO_Init(GPIOA, &gpio); \
        LCD_DC_Set(); \
    } while (0)

/* BL — PA11 (背光) */
#define LCD_BL_Clr()     GPIO_ResetBits(GPIOA, GPIO_Pin_11)
#define LCD_BL_Set()     GPIO_SetBits(GPIOA, GPIO_Pin_11)
#define LCD_BL_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_11; \
        gpio.GPIO_Mode  = GPIO_Mode_OUT; \
        gpio.GPIO_OType = GPIO_OType_PP; \
        gpio.GPIO_Speed = GPIO_Speed_Level_3; \
        gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL; \
        GPIO_Init(GPIOA, &gpio); \
        lcd_bl_on(); \
    } while (0)

/* CS — PA15 */
#define LCD_CS_Clr()    do{GPIOA->BRR = GPIO_Pin_15;}while(0)
#define LCD_CS_Set()    do{GPIOA->BSRR = GPIO_Pin_15;}while(0)
#define LCD_CS_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_15; \
        gpio.GPIO_Mode  = GPIO_Mode_OUT; \
        gpio.GPIO_OType = GPIO_OType_PP; \
        gpio.GPIO_Speed = GPIO_Speed_Level_3; \
        gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL; \
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
void lcd_dma_irq_handler(void);

#endif
