/**************************************************************************//**
 * @file     FlashPrg.c
 * @brief    Flash Programming Functions adapted for STM32F103 + W25Q128 (SPI2)
 * @version  V1.0.0
 * @date     10. January 2018
 ******************************************************************************/
/*
 * Copyright (c) 2010-2018 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "FlashOS.h"        // FlashOS Structures
#include "stm32f103_flash_spi_port.h"

/*
   Mandatory Flash Programming Functions (Called by FlashOS):
                int Init        (unsigned long adr,   // Initialize Flash
                                 unsigned long clk,
                                 unsigned long fnc);
                int UnInit      (unsigned long fnc);  // De-initialize Flash
                int EraseSector (unsigned long adr);  // Erase Sector Function
                int ProgramPage (unsigned long adr,   // Program Page Function
                                 unsigned long sz,
                                 unsigned char *buf);

   Optional  Flash Programming Functions (Called by FlashOS):
                int BlankCheck  (unsigned long adr,   // Blank Check
                                 unsigned long sz,
                                 unsigned char pat);
                int EraseChip   (void);               // Erase complete Device
      unsigned long Verify      (unsigned long adr,   // Verify Function
                                 unsigned long sz,
                                 unsigned char *buf);

       - BlanckCheck  is necessary if Flash space is not mapped into CPU memory space
       - Verify       is necessary if Flash space is not mapped into CPU memory space
       - if EraseChip is not provided than EraseSector for all sectors is called
*/

/* --------------------------------------------------------------------------
 * W25Q128 指令（GD25Q128 等指令集兼容的同类芯片也能用）
 * -------------------------------------------------------------------------- */
#define FLASH_CMD_PAGE_PROGRAM    0x02U
#define FLASH_CMD_READ_DATA       0x03U
#define FLASH_CMD_READ_STATUS1    0x05U
#define FLASH_CMD_WRITE_ENABLE    0x06U
#define FLASH_CMD_SECTOR_ERASE    0x20U /* 4KB 扇区擦除 */
#define FLASH_CMD_JEDEC_ID        0x9FU
#define FLASH_CMD_RELEASE_PWRDOWN 0xABU
#define FLASH_CMD_CHIP_ERASE      0xC7U

#define FLASH_STATUS1_BUSY        0x01U
#define FLASH_PAGE_SIZE           256U /* 芯片一次页编程的最大字节数 */

/* 器件起始地址（FlashDev.c 里的 Device Start Address）。
 * 指定放进已初始化数据段：随算法映像一起下载到 RAM，算法里没有需要调试器另行预留的清零数据区 */
static unsigned long base_adr __attribute__((section(".data")));

/*--------------------------------------------------------------
  * 名称: flash_cmd(uint8_t cmd)
  * 传入: cmd 指令
  * 功能: 发送一条不带参数的指令
----------------------------------------------------------------*/
static void flash_cmd(uint8_t cmd)
{
	FLASH_CS_Clr();
	(void)flash_xfer_1Byte(cmd);
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: flash_cmd_addr(uint8_t cmd, unsigned long addr)
  * 传入: cmd 指令 addr 芯片内地址
  * 功能: 片选拉低，发送指令和 24 位地址
  * 说明: 返回时片选仍为低，后面接着收发数据，由调用者拉高片选
----------------------------------------------------------------*/
static void flash_cmd_addr(uint8_t cmd, unsigned long addr)
{
	FLASH_CS_Clr();
	(void)flash_xfer_1Byte(cmd);
	(void)flash_xfer_1Byte((uint8_t)(addr >> 16));
	(void)flash_xfer_1Byte((uint8_t)(addr >> 8));
	(void)flash_xfer_1Byte((uint8_t)addr);
}

/*--------------------------------------------------------------
  * 名称: flash_read_status1()
  * 返回: 状态寄存器 1 的值（bit0 = BUSY）
  * 功能: 读状态寄存器 1
----------------------------------------------------------------*/
static uint8_t flash_read_status1(void)
{
	uint8_t status;

	FLASH_CS_Clr();
	(void)flash_xfer_1Byte(FLASH_CMD_READ_STATUS1);
	status = flash_xfer_1Byte(0x00U);
	FLASH_CS_Set();
	return status;
}

/*--------------------------------------------------------------
  * 名称: flash_wait_free()
  * 功能: 等待芯片内部的擦除 / 编程完成
----------------------------------------------------------------*/
static void flash_wait_free(void)
{
	while((flash_read_status1() & FLASH_STATUS1_BUSY) != 0U)
	{
	}
}

/*--------------------------------------------------------------
  * 名称: flash_read_jedec_id()
  * 返回: 制造商 ID | 存储类型 ID | 容量 ID（W25Q128 为 0xEF4018）
  * 功能: 读芯片 ID
----------------------------------------------------------------*/
static unsigned long flash_read_jedec_id(void)
{
	unsigned long id;

	FLASH_CS_Clr();
	(void)flash_xfer_1Byte(FLASH_CMD_JEDEC_ID);
	id = (unsigned long)flash_xfer_1Byte(0x00U) << 16;
	id |= (unsigned long)flash_xfer_1Byte(0x00U) << 8;
	id |= (unsigned long)flash_xfer_1Byte(0x00U);
	FLASH_CS_Set();
	return id;
}


/*
 *  Initialize Flash Programming Functions
 *    Parameter:      adr:  Device Base Address
 *                    clk:  Clock Frequency (Hz)
 *                    fnc:  Function Code (1 - Erase, 2 - Program, 3 - Verify)
 *    Return Value:   0 - OK,  1 - Failed
 */

int Init (unsigned long adr, unsigned long clk, unsigned long fnc)
{
	unsigned long id;

	(void)clk;
	(void)fnc;
	base_adr = adr;

	flash_port_init();

	/* 应用程序可能让芯片进了掉电模式，先唤醒（唤醒后要等几微秒才能收指令） */
	flash_cmd(FLASH_CMD_RELEASE_PWRDOWN);
	flash_delay(1000U);

	/* 没接芯片或接线不对时 MISO 读回全 1 / 全 0：直接报错，不往下执行 */
	if(flash_read_status1() == 0xFFU)
		return (1);
	flash_wait_free();
	id = flash_read_jedec_id();
	if((id == 0x000000UL) || (id == 0xFFFFFFUL))
		return (1);

	return (0);                                  // Finished without Errors
}


/*
 *  De-Initialize Flash Programming Functions
 *    Parameter:      fnc:  Function Code (1 - Erase, 2 - Program, 3 - Verify)
 *    Return Value:   0 - OK,  1 - Failed
 */

int UnInit (unsigned long fnc)
{
	(void)fnc;
	return (0);                                  // Finished without Errors
}


/*
 *  Erase complete Flash Memory
 *    Return Value:   0 - OK,  1 - Failed
 */

int EraseChip (void)
{
	flash_cmd(FLASH_CMD_WRITE_ENABLE);
	flash_cmd(FLASH_CMD_CHIP_ERASE);
	flash_wait_free();
	return (0);                                  // Finished without Errors
}


/*
 *  Erase Sector in Flash Memory
 *    Parameter:      adr:  Sector Address
 *    Return Value:   0 - OK,  1 - Failed
 */

int EraseSector (unsigned long adr)
{
	flash_cmd(FLASH_CMD_WRITE_ENABLE);
	flash_cmd_addr(FLASH_CMD_SECTOR_ERASE, adr - base_adr);
	FLASH_CS_Set();
	flash_wait_free();
	return (0);                                  // Finished without Errors
}


/*
 *  Program Page in Flash Memory
 *    Parameter:      adr:  Page Start Address
 *                    sz:   Page Size
 *                    buf:  Page Data
 *    Return Value:   0 - OK,  1 - Failed
 */

int ProgramPage (unsigned long adr, unsigned long sz, unsigned char *buf)
{
	unsigned long n;

	adr -= base_adr;

	/* 调试器一次下发的数据比芯片的页大，这里按芯片的 256 字节页边界拆开逐页写 */
	while(sz != 0U)
	{
		n = FLASH_PAGE_SIZE - (adr & (FLASH_PAGE_SIZE - 1U)); /* 当前页还能写多少字节 */
		if(n > sz)
			n = sz;

		flash_cmd(FLASH_CMD_WRITE_ENABLE);
		flash_cmd_addr(FLASH_CMD_PAGE_PROGRAM, adr);
		adr += n;
		sz -= n;
		while(n != 0U)
		{
			(void)flash_xfer_1Byte(*buf++);
			n--;
		}
		FLASH_CS_Set();
		flash_wait_free();
	}
	return (0);                                  // Finished without Errors
}


/*
 *  Blank Check Checks if Memory is Blank
 *    Parameter:      adr:  Block Start Address
 *                    sz:   Block Size (in bytes)
 *                    pat:  Block Pattern
 *    Return Value:   0 - OK,  1 - Failed
 */

int BlankCheck (unsigned long adr, unsigned long sz, unsigned char pat)
{
	int result = 0;

	/* 边读边比，不占用额外的 RAM 缓冲 */
	flash_cmd_addr(FLASH_CMD_READ_DATA, adr - base_adr);
	while(sz != 0U)
	{
		if(flash_xfer_1Byte(0x00U) != pat)
		{
			result = 1;                              /* 不是空的，需要擦除 */
			break;
		}
		sz--;
	}
	FLASH_CS_Set();
	return (result);
}


/*
 *  Verify Flash Contents
 *    Parameter:      adr:  Start Address
 *                    sz:   Size (in bytes)
 *                    buf:  Data
 *    Return Value:   (adr+sz) - OK, Failed Address
 */

unsigned long Verify (unsigned long adr, unsigned long sz, unsigned char *buf)
{
	unsigned long i;

	/* 边读边比，不占用额外的 RAM 缓冲 */
	flash_cmd_addr(FLASH_CMD_READ_DATA, adr - base_adr);
	for(i = 0U; i < sz; i++)
	{
		if(flash_xfer_1Byte(0x00U) != buf[i])
			break;                                   /* 校验失败，i 为出错位置 */
	}
	FLASH_CS_Set();
	return (adr + i);                            /* 全部一致时 i == sz */
}
