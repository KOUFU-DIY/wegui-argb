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

#include "stm32f103_flash_soft_stdspi_port.h"

/*--------------------------------------------------------------
  * 名称: flash_send_1Byte(uint8_t dat)
  * 传入: dat
  * 功能: SPI发送1个字节数据
----------------------------------------------------------------*/
void flash_send_1Byte(uint8_t dat)
{
	#if(0)
	//----FLASH MODE0 SPI SCL 默认常低,第一个跳变数据沿有效----
	uint8_t i;
	for(i=0;i<8;i++)
	{
		if(dat&0x80)
		   FLASH_MOSI_Set();
		else 
		   FLASH_MOSI_Clr();
		dat<<=1;
		FLASH_SCL_Set();
		FLASH_SCL_Clr();
	}
	#else
	//----FLASH MODE3 SPI SCL 默认常高,第二个跳变数据沿有效(与TFT屏幕一致)(默认选用)----
	uint8_t i;
	for(i=0;i<8;i++)
	{			  
		FLASH_SCL_Clr();
		if(dat&0x80)
		   FLASH_MOSI_Set();
		else 
		   FLASH_MOSI_Clr();
		dat<<=1; 
		FLASH_SCL_Set();
	}
	#endif
}

/*--------------------------------------------------------------
  * 名称: flash_send_nByte(uint8_t *p,uint32_t num)
  * 传入1: *p数组指针
  * 传入2: num发送数量
  * 返回: 无
  * 功能: 向flash发送num个数据
  * 说明: 
----------------------------------------------------------------*/
void flash_send_nByte(uint8_t *p,uint32_t num)
{
	uint32_t i=0;
	while(num>i)
	{
		flash_send_1Byte(p[i++]);	  
	}
}

/*--------------------------------------------------------------
  * 名称: flash_read_1Byte()
  * 功能: SPI发送1个字节数据
----------------------------------------------------------------*/
uint8_t flash_read_1Byte()
{
	#if(0)
	//----FLASH MODE0 SPI SCL 默认常低,第一个跳变数据沿有效----
	uint8_t i;
	uint8_t dat=0x00;
	for(i=0;i<8;i++)
	{			  
		FLASH_SCL_Set();
		dat<<=1; 
		if(FLASH_MISO_IO_Get())
		   dat|=0x01;
		FLASH_SCL_Clr();
	}
	return dat;
	#else
	//----FLASH MODE3 SPI SCL 默认常高,第二个跳变数据沿有效(与TFT屏幕一致)(默认选用)----
	uint8_t i;
	uint8_t dat=0x00;
	for(i=0;i<8;i++)
	{
		FLASH_SCL_Clr();
		dat<<=1;
		FLASH_SCL_Set();
		if(FLASH_MISO_IO_Get())
		   dat|=0x01;
	}
	return dat;
	#endif
}

/*--------------------------------------------------------------
  * 名称: flash_read_nByte(uint8_t *p,uint32_t num)
  * 传入1: *p数组指针
  * 传入2: num读取数量
  * 返回: 无
  * 功能: 向flash读取num个数据到p
  * 说明: 
----------------------------------------------------------------*/
void flash_read_nByte(uint8_t *p,uint32_t num)
{
	while(num--)
	{
		*p++ = flash_read_1Byte();	  
	}
}

/*--------------------------------------------------------------
  * 名称: flash_port_init()
  * 功能: flash接口初始化
----------------------------------------------------------------*/
void flash_port_init()
{
	
	FLASH_CS_IO_Init();
	FLASH_SCL_IO_Init();
	FLASH_MISO_IO_Init();
	FLASH_MOSI_IO_Init();
}


