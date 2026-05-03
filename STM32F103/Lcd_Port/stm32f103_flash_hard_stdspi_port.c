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
#include "stm32f103_hw_config.h"

#if(FLASH_PORT == _F_HARD_STDSPI)
#include "stm32f103_flash_hard_stdspi_port.h"

#ifndef RCC_HCLK_Divx
#	define RCC_HCLK_Divx RCC_HCLK_Div4
#endif

/*--------------------------------------------------------------
  * 名称: flash_cs_clr(void)
  * 功能: FLASH片选使能 源weak类型 改自flash_port_template.c
----------------------------------------------------------------*/
void flash_cs_clr(void)
{
	FLASH_CS_Clr();
}

/*--------------------------------------------------------------
  * 名称: flash_cs_set(void)
  * 功能: FLASH片选失能 weak类型 需要改写
----------------------------------------------------------------*/
void flash_cs_set(void)
{
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: flash_send_1Byte(uint8_t dat)
  * 传入: dat
  * 功能: SPI发送1个字节数据
----------------------------------------------------------------*/
void flash_send_1Byte(uint8_t dat)
{
	wait_flash_spi_txtemp_free();//等待SPI发送缓冲器为空
	send_flash_spi_dat(dat);
	read_flash_spi_done();//等待MISO接收完(同时MOSI发送完)
	read_flash_spi_dat();//读取一下,清空接收标志位
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
		wait_flash_spi_txtemp_free();//等待SPI发送缓冲器为空
		send_flash_spi_dat(p[i++]);	  
	}
	read_flash_spi_done();//等待MISO接收完(同时MOSI发送完)
	read_flash_spi_dat();//读取一下,清空接收标志位
}

/*--------------------------------------------------------------
  * 名称: flash_read_1Byte()
  * 功能: SPI发送1个字节数据
----------------------------------------------------------------*/
uint8_t flash_read_1Byte()
{
	wait_flash_spi_txtemp_free();//等待SPI发送缓冲器为空
	read_flash_spi_dat();//读取一下,清空接收标志位
	
	send_flash_spi_dat(0x00);//发送时钟
	read_flash_spi_done();//等待MISO接收完(同时MOSI发送完)
	return read_flash_spi_dat();
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
	wait_flash_spi_txtemp_free();//等待SPI发送缓冲器为空
	read_flash_spi_dat();//读取一下,清空接收标志位
	
	#if(1)
	//---方式1.发送完时钟后,读取完毕,再发送下一个
	while(num--)
	{
		send_flash_spi_dat(0x00);//发送时钟
		read_flash_spi_done();//等待rx接收完(同时MOSI发送完)
		*p++ = read_flash_spi_dat();  
	}
	
	#else
	//---方式2.发送完后立即发送下一个,再读,更节省时间
	send_flash_spi_dat(0x00);//发送时钟
	num--;
	while(num--)
	{
		wait_flash_spi_txtemp_free();//等待SPI发送缓冲器为空
		send_flash_spi_dat(0x00);//发送时钟
		//read_flash_spi_done();//等待MISO接收完(同时MOSI发送完)
		*p++ = read_flash_spi_dat(); 
	} 
	#endif
}

/*--------------------------------------------------------------
  * 名称: flash_port_init()
  * 功能: flash接口初始化
----------------------------------------------------------------*/
void flash_port_init()
{
	SPI_InitTypeDef SPI_InitStructure;
	//----FLASH MODE0 SPI SCL 默认常低,第一个跳变数据沿有效(默认选用)----
	#if(0)
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
	//----FLASH MODE3 SPI SCL 默认常高,第二个跳变数据沿有效----
	#else
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
	#endif
	
	SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
	SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
	SPI_InitStructure.SPI_BaudRatePrescaler = FLASH_SPI_BaudRatePrescaler_x;//分频(最快:SPI_BaudRatePrescaler_2)
	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial = 7;

	RCC_APB1PeriphClockCmd(FLASH_RCC_APB1Periph_SPIx, ENABLE);
	RCC_PCLK2Config(RCC_HCLK_Divx);//分频RCC_HCLK_Div1 RCC_HCLK_Div2 RCC_HCLK_Div4...
	
	SPI_Init(FLASH_SPIx, &SPI_InitStructure);
	SPI_Cmd(FLASH_SPIx, ENABLE);
	
	FLASH_CS_IO_Init();
	FLASH_SCL_IO_Init();
	FLASH_MISO_IO_Init();
	FLASH_MOSI_IO_Init();
}

#endif

