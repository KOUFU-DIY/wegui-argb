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

#include "stm32f103_flash_spi_port.h"

/* 等待 PLL 锁定 / 时钟切换的最大空循环次数（此时主频 8MHz，约几十毫秒） */
#define FLASH_CLOCK_TIMEOUT 0xFFFFU

/* 引脚模式：CRL / CRH 里每个引脚占 4 位（CNF[1:0] MODE[1:0]） */
#define FLASH_PIN_OUT_PP    0x3U /* 推挽输出 50MHz */
#define FLASH_PIN_AF_PP     0xBU /* 复用推挽输出 50MHz */
#define FLASH_PIN_IN_PULL   0x8U /* 上拉 / 下拉输入，输出锁存为 1 时是上拉 */

#if (FLASH_SPI_SOFT)
#define FLASH_SCK_Clr()     do{FLASH_SCK_PORT->BRR   = (uint32_t)1U << FLASH_SCK_PIN;}while(0)
#define FLASH_SCK_Set()     do{FLASH_SCK_PORT->BSRR  = (uint32_t)1U << FLASH_SCK_PIN;}while(0)
#define FLASH_MOSI_Clr()    do{FLASH_MOSI_PORT->BRR  = (uint32_t)1U << FLASH_MOSI_PIN;}while(0)
#define FLASH_MOSI_Set()    do{FLASH_MOSI_PORT->BSRR = (uint32_t)1U << FLASH_MOSI_PIN;}while(0)
#define FLASH_MISO_Get()    ((FLASH_MISO_PORT->IDR >> FLASH_MISO_PIN) & 1U)
#endif

/*--------------------------------------------------------------
  * 名称: flash_delay(uint32_t n)
  * 传入: n 空循环次数
  * 功能: 短延时（与主频有关，只用于不要求精确的等待）
----------------------------------------------------------------*/
void flash_delay(uint32_t n)
{
	volatile uint32_t i;

	for(i = 0U; i < n; i++)
	{
	}
}

/*--------------------------------------------------------------
  * 名称: flash_clock_init()
  * 功能: 系统时钟提到 64MHz（HSI/2 × 16），不依赖板上的晶振
  * 说明: 只在复位后的默认状态（系统时钟为 HSI、PLL 未开）下配置；其它状态
  *       说明时钟已经配置过，保持不动。PLL 锁不住时退回 HSI 继续工作（只是慢）。
----------------------------------------------------------------*/
static void flash_clock_init(void)
{
	uint32_t timeout;
	uint32_t reg;

	if(((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) || ((RCC->CR & RCC_CR_PLLON) != 0U))
		return;

	/* 片内 Flash 等待周期按 48~72MHz 设置（算法本身在 RAM 里运行，这里是给调试器读片内 Flash 用的） */
	FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_2;

	/* AHB = SYSCLK，APB2 = AHB，APB1 = AHB/2（APB1 上限 36MHz）；PLL = (HSI/2) × 16 = 64MHz */
	reg = RCC->CFGR;
	reg &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL);
	reg |= RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PLLMULL16;
	RCC->CFGR = reg;

	/* 开 PLL，等待锁定 */
	RCC->CR |= RCC_CR_PLLON;
	timeout = 0U;
	while(((RCC->CR & RCC_CR_PLLRDY) == 0U) && (timeout < FLASH_CLOCK_TIMEOUT))
	{
		timeout++;
	}

	if((RCC->CR & RCC_CR_PLLRDY) != 0U)
	{
		/* 系统时钟切到 PLL，等待切换完成 */
		RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
		timeout = 0U;
		while(((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) && (timeout < FLASH_CLOCK_TIMEOUT))
		{
			timeout++;
		}
	}
	else
	{
		RCC->CR &= ~RCC_CR_PLLON;
	}
}

/*--------------------------------------------------------------
  * 名称: flash_pin_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode)
  * 传入: port 端口 pin 引脚号(0~15) mode 引脚模式(FLASH_PIN_xxx)
  * 功能: 配置一个引脚的模式
----------------------------------------------------------------*/
static void flash_pin_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode)
{
	uint32_t pos = (pin & 7U) * 4U;

	if(pin < 8U)
		port->CRL = (port->CRL & ~((uint32_t)0xFU << pos)) | (mode << pos);
	else
		port->CRH = (port->CRH & ~((uint32_t)0xFU << pos)) | (mode << pos);
}

/*--------------------------------------------------------------
  * 名称: flash_xfer_1Byte(uint8_t dat)
  * 传入: dat 发出的字节
  * 返回: 同时收到的字节
  * 功能: SPI 全双工收发 1 个字节
----------------------------------------------------------------*/
#if (FLASH_SPI_SOFT)
uint8_t flash_xfer_1Byte(uint8_t dat)
{
	uint32_t i;

	/* 模式 3：时钟空闲为高。下降沿换数据（芯片同时送出一位），上升沿后读入一位 */
	for(i = 0U; i < 8U; i++)
	{
		FLASH_SCK_Clr();
		if((dat & 0x80U) != 0U)
			FLASH_MOSI_Set();
		else
			FLASH_MOSI_Clr();
		dat <<= 1;
		FLASH_SCK_Set();
		if(FLASH_MISO_Get() != 0U)
			dat |= 0x01U;
	}
	return dat;
}
#else
uint8_t flash_xfer_1Byte(uint8_t dat)
{
	/* 等到收完才返回：返回时这个字节已经完整移出，调用者可以马上动片选 */
	while((FLASH_SPIx->SR & SPI_SR_TXE) == 0U)
	{
	}
	FLASH_SPIx->DR = dat;
	while((FLASH_SPIx->SR & SPI_SR_RXNE) == 0U)
	{
	}
	return (uint8_t)FLASH_SPIx->DR;
}
#endif

/*--------------------------------------------------------------
  * 名称: flash_port_init()
  * 功能: 系统时钟、引脚、SPI 初始化
  * 说明: 可重复调用（擦除 / 编程 / 校验三个阶段各调用一次）
----------------------------------------------------------------*/
void flash_port_init(void)
{
	flash_clock_init();

	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPDEN | RCC_APB2ENR_IOPEEN;

	/* 片选先把输出锁存置高，再切成输出模式，引脚上不会出现低电平毛刺 */
	FLASH_CS_Set();
	flash_pin_mode(FLASH_CS_PORT, FLASH_CS_PIN, FLASH_PIN_OUT_PP);

	/* MISO 上拉输入：没接 Flash 时读回全 1，便于判断 */
	FLASH_MISO_PORT->BSRR = (uint32_t)1U << FLASH_MISO_PIN;
	flash_pin_mode(FLASH_MISO_PORT, FLASH_MISO_PIN, FLASH_PIN_IN_PULL);

#if (FLASH_SPI_SOFT)
	/* 时钟空闲为高，同样先置好输出锁存再切成输出模式 */
	FLASH_SCK_Set();
	FLASH_MOSI_Set();
	flash_pin_mode(FLASH_SCK_PORT, FLASH_SCK_PIN, FLASH_PIN_OUT_PP);
	flash_pin_mode(FLASH_MOSI_PORT, FLASH_MOSI_PIN, FLASH_PIN_OUT_PP);
#else
	/* SPI2 复位后重新配置：主机、全双工、模式 0（时钟空闲为低、第一个沿采样）、
	 * 8 位、高位在前、软件片选、时钟为 APB1 的 2 分频 */
	RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
	RCC->APB1RSTR |= RCC_APB1RSTR_SPI2RST;
	RCC->APB1RSTR &= ~RCC_APB1RSTR_SPI2RST;
	FLASH_SPIx->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI;
	FLASH_SPIx->CR1 |= SPI_CR1_SPE;

	flash_pin_mode(FLASH_SCK_PORT, FLASH_SCK_PIN, FLASH_PIN_AF_PP);
	flash_pin_mode(FLASH_MOSI_PORT, FLASH_MOSI_PIN, FLASH_PIN_AF_PP);
#endif
}
