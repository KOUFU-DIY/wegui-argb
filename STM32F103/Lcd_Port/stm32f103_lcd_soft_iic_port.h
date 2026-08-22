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

#ifndef STM32F103_LCD_SOFT_IIC_PORT_H
#define STM32F103_LCD_SOFT_IIC_PORT_H

#include "stdint.h"
#include "stm32f10x.h"

/* 软件 I2C（位翻转，开漏 + 模块上拉）：面向页式单色 OLED / 灰度 OLED
 * （SSD13xx/SH11xx/SSD1327 的 I2C 接法），TFT 不支持（带宽不够，
 * hw_config 有 #error 防呆）。
 * 帧格式：START + (OLED_IIC_7ADDR<<1) + 控制字节 + 载荷 + STOP，
 * 控制字节 0x00 = 后续全为命令，0x40 = 后续全为数据；从机地址在
 * stm32f103_hw_config.h 的 OLED_IIC_7ADDR 配置（0x3C/0x3D）。
 * 引脚与硬件 I2C2 复用位置兼容（为将来的硬件 I2C 端口预留）；
 * 模块无 RES 引脚时 PA4 悬空无影响。 */

/* ---- GPIO 宏 ---- */

/* SCL — PB10（推挽：时钟只由主机驱动，OLED 从机不做时钟拉伸，
 * 边沿不依赖上拉电阻，速率更稳） */
#define LCD_SCL_Clr()     do { GPIOB->BRR  = GPIO_Pin_10; } while (0)
#define LCD_SCL_Set()     do { GPIOB->BSRR = GPIO_Pin_10; } while (0)
#define LCD_SCL_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_10; \
        gpio.GPIO_Speed = GPIO_Speed_50MHz; \
        gpio.GPIO_Mode  = GPIO_Mode_Out_PP; \
        GPIO_Init(GPIOB, &gpio); \
        LCD_SCL_Set(); \
    } while (0)

/* SDA — PB11（开漏；置高即释放总线，可回读 ACK） */
#define LCD_SDA_Clr()     do { GPIOB->BRR  = GPIO_Pin_11; } while (0)
#define LCD_SDA_Set()     do { GPIOB->BSRR = GPIO_Pin_11; } while (0)
#define LCD_SDA_Read()    ((GPIOB->IDR & GPIO_Pin_11) != 0U)
#define LCD_SDA_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_11; \
        gpio.GPIO_Speed = GPIO_Speed_50MHz; \
        gpio.GPIO_Mode  = GPIO_Mode_Out_OD; \
        GPIO_Init(GPIOB, &gpio); \
        LCD_SDA_Set(); \
    } while (0)

/* RES — PA4 */
#define LCD_RES_Clr()     do { GPIOA->BRR  = GPIO_Pin_4; } while (0)
#define LCD_RES_Set()     do { GPIOA->BSRR = GPIO_Pin_4; } while (0)
#define LCD_RES_IO_Init() \
    do { \
        GPIO_InitTypeDef gpio; \
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); \
        gpio.GPIO_Pin   = GPIO_Pin_4; \
        gpio.GPIO_Speed = GPIO_Speed_50MHz; \
        gpio.GPIO_Mode  = GPIO_Mode_Out_PP; \
        GPIO_Init(GPIOA, &gpio); \
        LCD_RES_Set(); \
    } while (0)

/* 每个电平相位的延时节拍（__NOP 个数）：0 = 全速（72MHz 下约 1MHz 时钟），
 * 屏不认时逐步加大（4 约对应 400kHz 量级） */
#ifndef LCD_IIC_DELAY_NOPS
#define LCD_IIC_DELAY_NOPS 1
#endif

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
#if ((LCD_TYPE == HW_LCD_OLED) || (LCD_TYPE == HW_LCD_GRAY))
void lcd_set_windows(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
#endif
#if (LCD_TYPE == HW_LCD_OLED)
void lcd_oled_port(uint8_t *gram, uint32_t pix_size);
#endif
#if (LCD_TYPE == HW_LCD_GRAY)
void lcd_gray_port(uint8_t *gram, uint32_t pix_size);
#endif

#endif
