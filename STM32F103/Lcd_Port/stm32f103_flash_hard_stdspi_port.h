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
#ifndef STM32F103_FLASH_HARD_STDSPI_PORT_H
#define STM32F103_FLASH_HARD_STDSPI_PORT_H

#include "stm32f10x.h"
#include "stdint.h"

//-----------------硬件SPI配置---------------- 
#define FLASH_SPIx                 SPI2
#define FLASH_RCC_APB1Periph_SPIx  RCC_APB1Periph_SPI2

////---库函数版本---
////等待SPI发送缓冲器为空
//#define wait_flash_spi_txtemp_free() do{while(SPI_I2S_GetFlagStatus(FLASH_SPIx,SPI_I2S_FLAG_TXE)== RESET);}while(0)
////等待SPI发送器空闲(发完)
//#define send_flash_spi_done()        do{while(SPI_I2S_GetFlagStatus(FLASH_SPIx,SPI_I2S_FLAG_BSY));}while(0)
////等待SPI接收到一个字节
//#define read_flash_spi_done()        do{while(SPI_I2S_GetFlagStatus(FLASH_SPIx,SPI_I2S_FLAG_RXNE) == RESET);}while(0)
////向SPI发送缓冲器发送一个数据
//#define send_flash_spi_dat(dat)      do{SPI_I2S_SendData(FLASH_SPIx, dat);}while(0)
////向SPI读取一个数据
//#define read_flash_spi_dat(dat)      (SPI_I2S_ReceiveData(FLASH_SPIx))



////---寄存器版本---
//等待SPI发送缓冲器为空
#define wait_flash_spi_txtemp_free() do{while((FLASH_SPIx->SR & SPI_I2S_FLAG_TXE) == RESET);}while(0)
//等待SPI发送器空闲(发完)
#define send_flash_spi_done()        do{while(FLASH_SPIx->SR & SPI_I2S_FLAG_BSY);}while(0)
//等待SPI接收到一个字节
#define read_flash_spi_done()        do{while((FLASH_SPIx->SR & SPI_I2S_FLAG_RXNE) == RESET);}while(0)
//向SPI发送缓冲器发送一个数据
#define send_flash_spi_dat(dat)      do{FLASH_SPIx->DR = dat;}while(0)
//向SPI读取一个数据
#define read_flash_spi_dat()        (FLASH_SPIx->DR)

//-----------------IO接口定义---------------- 
//-----SCL-----
#define FLASH_SCL_IO_Init() \
	do{\
		GPIO_InitTypeDef GPIO_InitStruct;\
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);\
		GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_13;\
		GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;\
		GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF_PP;\
		GPIO_Init(GPIOB, &GPIO_InitStruct);\
	}while(0)

//-----MISO-----
#define FLASH_MISO_IO_Init() \
	do{\
		GPIO_InitTypeDef GPIO_InitStruct;\
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);\
		GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_14;\
		GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;\
		GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_IPU;\
		GPIO_Init(GPIOB, &GPIO_InitStruct);\
	}while(0)

//-----MOSI-----
#define FLASH_MOSI_IO_Init() \
	do{\
		GPIO_InitTypeDef GPIO_InitStruct;\
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);\
		GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_15;\
		GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;\
		GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF_PP;\
		GPIO_Init(GPIOB, &GPIO_InitStruct);\
	}while(0)

//-----CS-----
//#define FLASH_CS_Clr()  do{GPIO_ResetBits(GPIOB,GPIO_Pin_12);}while(0)//库函数操作IO
//#define FLASH_CS_Set()  do{GPIO_SetBits(GPIOB,GPIO_Pin_12);}while(0)//库函数操作IO
#define FLASH_CS_Clr() do{GPIOB->BRR = GPIO_Pin_12;}while(0)//寄存器操作,节省函数调用时间
#define FLASH_CS_Set() do{GPIOB->BSRR = GPIO_Pin_12;}while(0)//寄存器操作,节省函数调用时间
#define FLASH_CS_IO_Init() \
	do{\
		GPIO_InitTypeDef GPIO_InitStruct;\
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);\
		GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_12;\
		GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;\
		GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_Out_PP;\
		GPIO_Init(GPIOB, &GPIO_InitStruct);\
		FLASH_CS_Set();\
	}while(0)

	
	
	
/*--------------------------------------------------------------
  * 名称: flash_cs_clr(void)
  * 功能: FLASH片选使能
----------------------------------------------------------------*/
void flash_cs_clr(void);

/*--------------------------------------------------------------
  * 名称: flash_cs_set(void)
  * 功能: FLASH片选失能
----------------------------------------------------------------*/
void flash_cs_set(void);
	
/*--------------------------------------------------------------
  * 名称: flash_send_1Byte(uint8_t dat)
  * 传入: dat
  * 返回: 无
  * 功能: SPI发送1个字节数据
  * 说明: 
----------------------------------------------------------------*/
void flash_send_1Byte(uint8_t dat);
	
/*--------------------------------------------------------------
  * 名称: flash_send_nByte(uint8_t *p,uint32_t num)
  * 传入1: *p数组指针
  * 传入2: num发送数量
  * 返回: 无
  * 功能: 向flash发送num个数据
  * 说明: 
----------------------------------------------------------------*/
void flash_send_nByte(uint8_t *p,uint32_t num);
	
/*--------------------------------------------------------------
  * 名称: flash_read_1Byte()
  * 功能: SPI发送1个字节数据
----------------------------------------------------------------*/
uint8_t flash_read_1Byte(void);

/*--------------------------------------------------------------
  * 名称: flash_read_nByte(uint8_t *p,uint32_t num)
  * 传入1: *p数组指针
  * 传入2: num读取数量
  * 返回: 无
  * 功能: 向flash读取num个数据到p
  * 说明: 
----------------------------------------------------------------*/
void flash_read_nByte(uint8_t *p,uint32_t num);
	
/*--------------------------------------------------------------
  * 名称: flash_port_init()
  * 功能: flash接口初始化
----------------------------------------------------------------*/
void flash_port_init(void);
	
#endif
