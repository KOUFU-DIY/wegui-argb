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

#include "w25qxx.h"


/*--------------------------------------------------------------
  * 名称: w25qxx_write_enable(void)
  * 功能: 写使能
----------------------------------------------------------------*/
static void w25qxx_write_enable(void)
{
	FLASH_CS_Clr();
	flash_send_1Byte(0x06);
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_volatile_sr_write_enable(void)
  * 功能: 状态寄存器写使能
----------------------------------------------------------------*/
static void w25qxx_volatile_sr_write_enable(void)
{
	FLASH_CS_Clr();
	flash_send_1Byte(0x50);//Volatile SR Write Enable
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_write_disable(void)
  * 功能: 写禁止
----------------------------------------------------------------*/
static void w25qxx_write_disable(void)
{
	FLASH_CS_Clr();
	flash_send_1Byte(0x04);
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_read_status1(void)
  * 反回: 状态寄存器1的值
  * 功能: 获取芯片状态寄存器1
  * 说明: 
  * [b7]SRP Status Register Protect 0
  * [b6]SEC Sector Protect Bit
  * [b5]TB Top/Bottom Protect Bit
  * [b4]BP2 Block Protect Bits
  * [b3]BP1 Block Protect Bits
  * [b2]BP0 Block Protect Bits
  * [b1]WRITE_EN 
  * [b0]BUSY
----------------------------------------------------------------*/
uint8_t w25qxx_read_status1(void)
{
	uint8_t i;
	FLASH_CS_Clr();
	flash_send_1Byte(0x05);
	i = flash_read_1Byte();
	FLASH_CS_Set();
	return i;
}

/*--------------------------------------------------------------
  * 名称: w25qxx_write_status1(uint8_t i)
	* 传入: i寄存器新值
  * 功能: 更新状态寄存器1
  * 说明: 
  * [b7]SRP Status Register Protect 0
  * [b6]SEC Sector Protect Bit
  * [b5]TB Top/Bottom Protect Bit
  * [b4]BP2 Block Protect Bits
  * [b3]BP1 Block Protect Bits
  * [b2]BP0 Block Protect Bits
  * [b1]WRITE_EN 只读
  * [b0]BUSY 只读
----------------------------------------------------------------*/
void w25qxx_write_status1(uint8_t i)
{
	w25qxx_volatile_sr_write_enable();
	FLASH_CS_Clr();
	flash_send_1Byte(0x01);
	flash_send_1Byte(i);
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_read_status2(void)
  * 反回: 状态寄存器2的值
  * 功能: 读取状态寄存器2
  * 说明: 
  * [b7]SUS Suspend Status
  * [b6]CMP Complement Protect
  * [b5]LB3 Security Register Lock Bits
  * [b4]LB2 Security Register Lock Bits
  * [b3]LB1 Security Register Lock Bits
  * [b2]Reserved
  * [b1]QE Quad Enable
  * [b0]SRL Status Register Protect 1
----------------------------------------------------------------*/
uint8_t w25qxx_read_status2(void)
{
	uint8_t i;
	FLASH_CS_Clr();
	flash_send_1Byte(0x35);
	i = flash_read_1Byte();
	FLASH_CS_Set();
	return i;
}

/*--------------------------------------------------------------
  * 名称: w25qxx_read_status2(uint8_t i)
	* 传入: i寄存器新值
  * 功能: 写入状态寄存器2
  * 说明: 
  * [b7]SUS Suspend Status
  * [b6]CMP Complement Protect
  * [b5]LB3 Security Register Lock Bits
  * [b4]LB2 Security Register Lock Bits
  * [b3]LB1 Security Register Lock Bits
  * [b2]Reserved
  * [b1]QE Quad Enable
  * [b0]SRL Status Register Protect 1
----------------------------------------------------------------*/
void w25qxx_write_status2(uint8_t i)
{
	w25qxx_volatile_sr_write_enable();
	FLASH_CS_Clr();
	flash_send_1Byte(0x31);
	flash_send_1Byte(i);
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_read_status3(void)
	* 传入: i寄存器新值
  * 功能: 写入状态寄存器3
  * 说明: 
  * [b7]HOLD/RST /HOLD or /RESET Function
  * [b6]DRV1 Output Driver Strength
  * [b5]DRV0 Output Driver Strength
  * [b4]Reserved
  * [b3]Reserved
  * [b2]WPS Write Protect Selection
  * [b1]Reserved
  * [b0]Reserved
----------------------------------------------------------------*/
uint8_t w25qxx_read_status3(void)
{
	uint8_t i;
	FLASH_CS_Clr();
	flash_send_1Byte(0x15);
	i = flash_read_1Byte();
	FLASH_CS_Set();
	return i;
}

/*--------------------------------------------------------------
  * 名称: w25qxx_write_status3(uint8_t i)
	* 传入: i寄存器新值
  * 功能: 写入状态寄存器2
  * 说明: 
  * [b7]HOLD/RST /HOLD or /RESET Function
  * [b6]DRV1 Output Driver Strength
  * [b5]DRV0 Output Driver Strength
  * [b4]Reserved
  * [b3]Reserved
  * [b2]WPS Write Protect Selection
  * [b1]Reserved
  * [b0]Reserved
----------------------------------------------------------------*/
void w25qxx_write_status3(uint8_t i)
{
	w25qxx_volatile_sr_write_enable();
	FLASH_CS_Clr();
	flash_send_1Byte(0x11);
	flash_send_1Byte(i);
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_chip_erase(void)
  * 功能: 擦除整颗芯片
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_chip_erase(void)
{
	w25qxx_write_enable();
	FLASH_CS_Clr();
	flash_send_1Byte(0xC7);//或60h
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_erase_program_suspend(void)
  * 功能: 暂停擦写动作
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_erase_program_suspend(void)
{
	//未测试
	FLASH_CS_Clr();
	flash_send_1Byte(0x75);//Erase / Program Suspend 
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_erase_program_resume(void)
  * 功能: 恢复擦写动作
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_erase_program_resume(void)
{
	//未测试
	FLASH_CS_Clr();
	flash_send_1Byte(0x7A);//Erase / Program Resume
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_powerdown(void)
  * 功能: 进入休眠
----------------------------------------------------------------*/
void w25qxx_powerdown(void)
{
	FLASH_CS_Clr();
	flash_send_1Byte(0xB9);
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_release_powerdown(void)
  * 返回: 设备id
  * 功能: 退出休眠
----------------------------------------------------------------*/
uint8_t w25qxx_release_powerdown(void)
{
	uint8_t i;
	FLASH_CS_Clr();
	flash_send_1Byte(0xAB);
	flash_read_1Byte();//Dummy
	flash_read_1Byte();//Dummy
	flash_read_1Byte();//Dummy
	i=flash_read_1Byte();//返回设备id
	FLASH_CS_Set();
	return i;
}

/*--------------------------------------------------------------
  * 名称: w25qxx_read_manufacturer_device_id(void)
  * 反回: 制造商id 组合 设备id
  * 功能: 读取芯片信息
  * 说明: 
----------------------------------------------------------------*/
uint16_t w25qxx_read_manufacturer_device_id(void)
{
	static uint16_t id;
	FLASH_CS_Clr();
	flash_send_1Byte(0x90);//获取ID
	flash_send_1Byte(0xFF);//任意
	flash_send_1Byte(0xFF);//任意
	flash_send_1Byte(0x00);//固定发到0x00;
	id=flash_read_1Byte()<<8;//获取制造商id
	id|=flash_read_1Byte();//获取设备id
	FLASH_CS_Set();
	return id;
}

/*--------------------------------------------------------------
  * 名称: w25qxx_read_jedec_id(void)
  * 反回: 制造商id | Memory Type ID | Capacity ID
  * 功能: 读取芯片信息
  * 说明: 
----------------------------------------------------------------*/
uint32_t w25qxx_read_jedec_id(void)
{
	static uint32_t id;
	FLASH_CS_Clr();
	flash_send_1Byte(0x9F);//获取ID
	id=flash_read_1Byte()<<16;//获取制造商Manufacturer ID
	id|=flash_read_1Byte()<<8;//Memory Type ID15-8
	id|=flash_read_1Byte();//Capacity ID7-0
	FLASH_CS_Set();
	return id;
}

/*--------------------------------------------------------------
  * 名称: w25qxx_global_block_lock(void)
  * 反回: 无
  * 功能: 全区块锁定
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_global_block_lock(void)
{
	FLASH_CS_Clr();
	flash_send_1Byte(0x7E);//Global Block Lock
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_global_block_unlock(void)
  * 反回: 无
  * 功能: 全区块解锁
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_global_block_unlock(void)
{
	FLASH_CS_Clr();
	flash_send_1Byte(0x98);//Global Block Unlock
	FLASH_CS_Set();
}
/*--------------------------------------------------------------
  * 名称: w25qxx_enter_qspi_mode(void)
  * 反回: 无
  * 功能: 进入QSPI模式
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_enter_qspi_mode(void)
{
	FLASH_CS_Clr();
	flash_send_1Byte(0x38);//Enter QPI Mode
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_exit_qspi_mode(void)
  * 反回: 无
  * 功能: 退出QSPI模式
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_exit_qspi_mode(void)
{
	FLASH_CS_Clr();
	flash_send_1Byte(0xFF);//Exit QPI Mode
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_reset(void)
  * 反回: 无
  * 功能: 复位
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_reset(void)
{
	FLASH_CS_Clr();
	flash_send_1Byte(0x66);//Enable Reset
	flash_send_1Byte(0x99);//Reset Devic
	FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_read_unique_id(void)
  * 反回: Unique ID
  * 功能: 获取Unique ID,返回uint8数组指针
  * 说明: 
----------------------------------------------------------------*/
uint8_t* w25qxx_read_unique_id(void)
{
	static uint8_t uniqueid[8];
	FLASH_CS_Clr();
	flash_send_1Byte(0x4B);//Read Unique ID
	flash_read_1Byte();//Dummy
	flash_read_1Byte();//Dummy
	flash_read_1Byte();//Dummy
	flash_read_1Byte();//Dummy
	uniqueid[0] =(uint32_t)flash_read_1Byte();//UID63-UID0
	uniqueid[1]=(uint32_t)flash_read_1Byte();
	uniqueid[2]=(uint32_t)flash_read_1Byte();
	uniqueid[3]=(uint32_t)flash_read_1Byte();
	uniqueid[4]=(uint32_t)flash_read_1Byte();//UID63-UID0
	uniqueid[5]=(uint32_t)flash_read_1Byte();
	uniqueid[6]=(uint32_t)flash_read_1Byte();
	uniqueid[7]=(uint32_t)flash_read_1Byte();
	FLASH_CS_Set();
	return uniqueid;
}

/*--------------------------------------------------------------
  * 名称: w25qxx_page_program(void)
  * 传入: addr地址 pbuf数据指针 lenth数据长度(长度不能大于256)
  * 功能: 指定页编程
  * 说明: 数据长度禁止跨页输入
----------------------------------------------------------------*/
static void w25qxx_1page_program(uint32_t addr,uint8_t *pbuf,uint16_t lenth)
{
		w25qxx_write_enable();
		FLASH_CS_Clr();
		
		flash_send_1Byte(0x02);//Page Program
		flash_send_1Byte((addr>>16)&0xFF);//地址
		flash_send_1Byte((addr>>8)&0xFF);//地址
		flash_send_1Byte((addr>>0)&0xFF);//地址
		flash_send_nByte(pbuf,lenth);//连续发送数据
		FLASH_CS_Set();
		w25qxx_wait_free();//等待写完
}

/*--------------------------------------------------------------
  * 名称: w25qxx_write(uint32_t addr,uint8_t *pbuf,uint32_t lenth)
  * 传入: addr地址 pbuf数据指针 lenth数据长度(长度不能大于256)
  * 功能: 任意地址和长度写入
  * 说明: 支持任意跨页写入
----------------------------------------------------------------*/
void w25qxx_write(uint32_t addr,uint8_t *pbuf,uint32_t lenth)
{
	uint32_t pageremain;
	pageremain = 256-addr%256; //所选地址,"页剩余的字节数"
	if( lenth<=pageremain )	//要写入的字节小于"页剩余的字节数"
	{
		pageremain=lenth;
	}
	while(1)
	{	   
		w25qxx_1page_program(addr,pbuf,pageremain); //在对应的地址下写入数据
		if(pageremain == lenth)//判断是否已经写完
		{
			break;//写完了
		}
	 	else//还没写完
		{
			pbuf += pageremain;		//移动数组
			addr += pageremain;		//计算下一组开始写的地址位置
 
			lenth -= pageremain;	//减去已经写入了的字节数
			if(lenth>=256)
			{
				pageremain=256; //继续一次写入256个字节
			}
			else 
			{
				pageremain=lenth;//剩余不足256个字节了，就直接等于当前字节数
			}
		}
	}
}

/*--------------------------------------------------------------
  * 名称: w25qxx_sector_erase(uint32_t addr)
  * 传入: addr地址
  * 功能: 输入地址对应的扇区(4k)擦除
  * 说明: 需要时间等待busy
----------------------------------------------------------------*/
void w25qxx_sector_erase(uint32_t addr)
{
	w25qxx_write_enable();
	FLASH_CS_Clr();
	flash_send_1Byte(0x20);//Sector Erase
	flash_send_1Byte((addr>>16)&0xFF);//地址
	flash_send_1Byte((addr>>8)&0xFF);//地址
	flash_send_1Byte((addr>>0)&0xFF);//地址
	FLASH_CS_Set();
	w25qxx_wait_free();//等待擦完
}

/*--------------------------------------------------------------
  * 名称: w25qxx_read_data(uint32_t addr,uint8_t p[],uint32_t lenth);
  * 传入: addr地址 p数据指针 lenth数据长度
  * 功能: 连续读取 数据放到p里
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_read_data(uint32_t addr,uint8_t p[],uint32_t lenth)
{
		FLASH_CS_Clr();
		flash_send_1Byte(0x03);//Read Data
		flash_send_1Byte((addr>>16)&0xFF);//地址
		flash_send_1Byte((addr>>8)&0xFF);//地址
		flash_send_1Byte((addr>>0)&0xFF);//地址
		flash_read_nByte(p,lenth);//连续读取
		FLASH_CS_Set();
}

/*--------------------------------------------------------------
  * 名称: w25qxx_is_busy(void)
  * 反回: 0空闲 1忙碌中
  * 功能: 检测模块是否忙碌
  * 说明: 
----------------------------------------------------------------*/
uint8_t w25qxx_is_busy(void)
{
	uint8_t i;
	FLASH_CS_Clr();
	flash_send_1Byte(0x05);
	i = flash_read_1Byte()&0x01;
	FLASH_CS_Set();
	return i;
}

/*--------------------------------------------------------------
  * 名称: w25qxx_wait_free(void)
  * 反回: 0空闲 1忙碌中
  * 功能: 检测模块是否忙碌
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_wait_free(void)
{
	while(w25qxx_is_busy()!=0);
}


//Block Erase (32KB)
//Block Erase (64KB)
//----Read Data
//Fast Read
//Fast Read Dual Output
//Fast Read Quad Output
//Read SFDP Register
//Erase Security Register
//Program Security Register
//Read Security Register
//Individual Block Lock
//Individual Block Unlock
//Read Block Lock
//Fast Read Dual I/O
//Mftr./Device ID Dual I/O
//Set Burst with Wrap 
//Fast Read Quad I/O
//Word Read Quad I/O
//Octal Word Read Quad I/O
//Mftr./Device ID Quad I/O
