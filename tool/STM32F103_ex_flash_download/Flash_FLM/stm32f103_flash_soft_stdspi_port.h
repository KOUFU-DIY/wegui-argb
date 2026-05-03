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
#ifndef STM32F103_FLASH_SOFT_STDSPI_PORT_H
#define STM32F103_FLASH_SOFT_STDSPI_PORT_H

#include "stm32f10x.h"
#include "stdint.h"

//-----------------IO接口定义---------------- 
//-----SCL-----
//#define FLASH_SCL_Clr() do{GPIO_ResetBits(GPIOB,GPIO_Pin_13);}while(0)//库函数操作IO
//#define FLASH_SCL_Set() do{GPIO_SetBits(GPIOB,GPIO_Pin_13);}while(0)//库函数操作IO
#define FLASH_SCL_Clr() do{GPIOB->BRR = GPIO_Pin_13;}while(0)//直接寄存器操作,节省函数调用时间
#define FLASH_SCL_Set() do{GPIOB->BSRR = GPIO_Pin_13;}while(0)//直接寄存器操作,节省函数调用时间
#define FLASH_SCL_IO_Init() \
	do{\
		GPIO_InitTypeDef GPIO_InitStruct;\
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);\
		GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_13;\
		GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;\
		GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_Out_PP;\
		GPIO_Init(GPIOB, &GPIO_InitStruct);\
		FLASH_SCL_Set();\
	}while(0)

//-----MISO-----
#define FLASH_MISO_IO_Get() (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14))
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
//#define FLASH_MOSI_Clr() do{GPIO_ResetBits(GPIOB,GPIO_Pin_15);}while(0)//库函数操作IO
//#define FLASH_MOSI_Set() do{GPIO_SetBits(GPIOB,GPIO_Pin_15);}while(0)//库函数操作IO
#define FLASH_MOSI_Clr() do{GPIOB->BRR = GPIO_Pin_15;}while(0)//寄存器操作,节省函数调用时间
#define FLASH_MOSI_Set() do{GPIOB->BSRR = GPIO_Pin_15;}while(0)//寄存器操作,节省函数调用时间
#define FLASH_MOSI_IO_Init() \
	do{\
		GPIO_InitTypeDef GPIO_InitStruct;\
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);\
		GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_15;\
		GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;\
		GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_Out_PP;\
		GPIO_Init(GPIOB, &GPIO_InitStruct);\
		FLASH_MOSI_Set();\
	}while(0)

//-----CS-----(可选)
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
	
void flash_port_init(void);
	
#endif
