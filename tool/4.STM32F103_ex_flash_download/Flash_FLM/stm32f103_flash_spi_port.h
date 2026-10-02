/*
	Copyright 2025 Lu Zhihao

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#ifndef STM32F103_FLASH_SPI_PORT_H
#define STM32F103_FLASH_SPI_PORT_H

#include <stdint.h>

/* --------------------------------------------------------------------------
 * STM32F103 外挂 Flash 烧录算法的硬件接口（寄存器直接操作）
 *
 * 烧录算法整体装进 MCU 的 RAM 里运行，所以不链接固件库，只在下面保留用到的寄存器定义。
 * 寄存器名、地址和位定义都与 stm32f10x.h 相同（结构体只列到用到的寄存器为止），
 * 对照手册或改写时可以直接查。
 *
 * 默认接线（与 STM32F103 工程 Lcd_Port 里的外挂 Flash 接口相同）：
 *   SCK  PB13 (SPI2_SCK)
 *   MISO PB14 (SPI2_MISO)   接 Flash 的 DO
 *   MOSI PB15 (SPI2_MOSI)   接 Flash 的 DI
 *   CS   PB12 (普通输出)
 * 换板子时改下面“引脚配置”一节，重新编译 Flash_FLM 工程即可。
 * -------------------------------------------------------------------------- */

/* ------------------------------ 寄存器定义 ------------------------------ */
typedef struct
{
	volatile uint32_t CR;
	volatile uint32_t CFGR;
	volatile uint32_t CIR;
	volatile uint32_t APB2RSTR;
	volatile uint32_t APB1RSTR;
	volatile uint32_t AHBENR;
	volatile uint32_t APB2ENR;
	volatile uint32_t APB1ENR;
	volatile uint32_t BDCR;
	volatile uint32_t CSR;
} RCC_TypeDef;

typedef struct
{
	volatile uint32_t ACR;
} FLASH_TypeDef;

typedef struct
{
	volatile uint32_t CRL;
	volatile uint32_t CRH;
	volatile uint32_t IDR;
	volatile uint32_t ODR;
	volatile uint32_t BSRR;
	volatile uint32_t BRR;
	volatile uint32_t LCKR;
} GPIO_TypeDef;

typedef struct
{
	volatile uint16_t CR1;
	uint16_t RESERVED0;
	volatile uint16_t CR2;
	uint16_t RESERVED1;
	volatile uint16_t SR;
	uint16_t RESERVED2;
	volatile uint16_t DR;
	uint16_t RESERVED3;
} SPI_TypeDef;

#define RCC                         ((RCC_TypeDef *)0x40021000UL)
#define FLASH                       ((FLASH_TypeDef *)0x40022000UL)
#define GPIOA                       ((GPIO_TypeDef *)0x40010800UL)
#define GPIOB                       ((GPIO_TypeDef *)0x40010C00UL)
#define GPIOC                       ((GPIO_TypeDef *)0x40011000UL)
#define GPIOD                       ((GPIO_TypeDef *)0x40011400UL)
#define GPIOE                       ((GPIO_TypeDef *)0x40011800UL)
#define SPI2                        ((SPI_TypeDef *)0x40003800UL)

/* RCC 时钟 */
#define RCC_CR_PLLON                ((uint32_t)0x01000000)
#define RCC_CR_PLLRDY               ((uint32_t)0x02000000)

#define RCC_CFGR_SW                 ((uint32_t)0x00000003)
#define RCC_CFGR_SW_PLL             ((uint32_t)0x00000002) /* 系统时钟选 PLL */
#define RCC_CFGR_SWS                ((uint32_t)0x0000000C)
#define RCC_CFGR_SWS_HSI            ((uint32_t)0x00000000) /* 当前系统时钟为 HSI */
#define RCC_CFGR_SWS_PLL            ((uint32_t)0x00000008) /* 当前系统时钟为 PLL */
#define RCC_CFGR_HPRE               ((uint32_t)0x000000F0)
#define RCC_CFGR_PPRE1              ((uint32_t)0x00000700)
#define RCC_CFGR_PPRE1_DIV2         ((uint32_t)0x00000400)
#define RCC_CFGR_PPRE2              ((uint32_t)0x00003800)
#define RCC_CFGR_PLLSRC             ((uint32_t)0x00010000)
#define RCC_CFGR_PLLXTPRE           ((uint32_t)0x00020000)
#define RCC_CFGR_PLLMULL            ((uint32_t)0x003C0000)
#define RCC_CFGR_PLLMULL16          ((uint32_t)0x00380000)

#define RCC_APB2ENR_IOPAEN          ((uint32_t)0x00000004)
#define RCC_APB2ENR_IOPBEN          ((uint32_t)0x00000008)
#define RCC_APB2ENR_IOPCEN          ((uint32_t)0x00000010)
#define RCC_APB2ENR_IOPDEN          ((uint32_t)0x00000020)
#define RCC_APB2ENR_IOPEEN          ((uint32_t)0x00000040)
#define RCC_APB1ENR_SPI2EN          ((uint32_t)0x00004000)
#define RCC_APB1RSTR_SPI2RST        ((uint32_t)0x00004000)

/* 片内 Flash 等待周期 */
#define FLASH_ACR_LATENCY           ((uint32_t)0x00000007)
#define FLASH_ACR_LATENCY_2         ((uint32_t)0x00000002)

/* SPI */
#define SPI_CR1_MSTR                ((uint16_t)0x0004)
#define SPI_CR1_SPE                 ((uint16_t)0x0040)
#define SPI_CR1_SSI                 ((uint16_t)0x0100)
#define SPI_CR1_SSM                 ((uint16_t)0x0200)
#define SPI_SR_RXNE                 ((uint16_t)0x0001)
#define SPI_SR_TXE                  ((uint16_t)0x0002)

/* ------------------------------ 引脚配置 ------------------------------ */
/* SPI 的实现方式
 * 0：硬件 SPI2。SCK / MISO / MOSI 固定为 PB13 / PB14 / PB15，片选可任意指定
 * 1：GPIO 模拟 SPI。四个引脚都可任意指定，速度比硬件 SPI 慢，烧录时间略长
 * 注意 PA13 / PA14 是下载口不能占用；PA15 / PB3 / PB4 复位后是 JTAG 功能，不能直接当普通引脚用 */
#define FLASH_SPI_SOFT              0

#define FLASH_CS_PORT               GPIOB
#define FLASH_CS_PIN                12U

#define FLASH_SCK_PORT              GPIOB
#define FLASH_SCK_PIN               13U

#define FLASH_MISO_PORT             GPIOB
#define FLASH_MISO_PIN              14U

#define FLASH_MOSI_PORT             GPIOB
#define FLASH_MOSI_PIN              15U

/* 硬件 SPI 用哪一路。SPI2 挂在 APB1 上，时钟取 APB1 的 2 分频：
 * 算法把系统时钟提到 64MHz、APB1 为 32MHz，SPI 时钟即 16MHz（STM32F103 的 SPI 上限是 18MHz） */
#define FLASH_SPIx                  SPI2

#define FLASH_CS_Clr()              do{FLASH_CS_PORT->BRR  = (uint32_t)1U << FLASH_CS_PIN;}while(0)
#define FLASH_CS_Set()              do{FLASH_CS_PORT->BSRR = (uint32_t)1U << FLASH_CS_PIN;}while(0)

/*--------------------------------------------------------------
  * 名称: flash_port_init()
  * 功能: 系统时钟、引脚、SPI 初始化
  * 说明: 可重复调用（擦除 / 编程 / 校验三个阶段各调用一次）
----------------------------------------------------------------*/
void flash_port_init(void);

/*--------------------------------------------------------------
  * 名称: flash_xfer_1Byte(uint8_t dat)
  * 传入: dat 发出的字节
  * 返回: 同时收到的字节
  * 功能: SPI 全双工收发 1 个字节
----------------------------------------------------------------*/
uint8_t flash_xfer_1Byte(uint8_t dat);

/*--------------------------------------------------------------
  * 名称: flash_delay(uint32_t n)
  * 传入: n 空循环次数
  * 功能: 短延时（与主频有关，只用于不要求精确的等待）
----------------------------------------------------------------*/
void flash_delay(uint32_t n);

#endif
