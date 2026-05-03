#ifndef W25QXX_H
#define W25QXX_H

#include "stdint.h"


/*--------------------------------------------------------------
  * 名称: w25qxx_write_enable(void)
  * 功能: 写使能
----------------------------------------------------------------*/
//static void w25qxx_write_enable(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_volatile_sr_write_enable(void)
  * 功能: 状态寄存器写使能
----------------------------------------------------------------*/
//static void w25qxx_volatile_sr_write_enable(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_write_disable(void)
  * 功能: 写禁止
----------------------------------------------------------------*/
//static void w25qxx_write_disable(void);

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
uint8_t w25qxx_read_status1(void);

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
void w25qxx_write_status1(uint8_t i);

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
uint8_t w25qxx_read_status2(void);

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
void w25qxx_write_status2(uint8_t i);

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
uint8_t w25qxx_read_status3(void);

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
void w25qxx_write_status3(uint8_t i);

/*--------------------------------------------------------------
  * 名称: w25qxx_chip_erase(void)
  * 功能: 擦除整颗芯片
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_chip_erase(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_erase_program_suspend(void)
  * 功能: 暂停擦写动作
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_erase_program_suspend(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_erase_program_resume(void)
  * 功能: 恢复擦写动作
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_erase_program_resume(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_powerdown(void)
  * 功能: 进入休眠
----------------------------------------------------------------*/
void w25qxx_powerdown(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_release_powerdown(void)
  * 返回: 设备id
  * 功能: 退出休眠
----------------------------------------------------------------*/
uint8_t w25qxx_release_powerdown(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_read_manufacturer_device_id(void)
  * 反回: 制造商id 组合 设备id
  * 功能: 读取芯片信息
  * 说明: 
----------------------------------------------------------------*/
uint16_t w25qxx_read_manufacturer_device_id(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_read_jedec_id(void)
  * 反回: 制造商id | Memory Type ID | Capacity ID
  * 功能: 读取芯片信息
  * 说明: 
----------------------------------------------------------------*/
uint32_t w25qxx_read_jedec_id(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_global_block_lock(void)
  * 反回: 无
  * 功能: 全区块锁定
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_global_block_lock(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_global_block_unlock(void)
  * 反回: 无
  * 功能: 全区块解锁
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_global_block_unlock(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_enter_qspi_mode(void)
  * 反回: 无
  * 功能: 进入QSPI模式
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_enter_qspi_mode(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_exit_qspi_mode(void)
  * 反回: 无
  * 功能: 退出QSPI模式
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_exit_qspi_mode(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_reset(void)
  * 反回: 无
  * 功能: 复位
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_reset(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_read_unique_id(void)
  * 反回: Unique ID
  * 功能: 获取Unique ID,返回uint8数组指针
  * 说明: 
----------------------------------------------------------------*/
uint8_t* w25qxx_read_unique_id(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_page_program(void)
  * 传入: addr地址 pbuf数据指针 lenth数据长度(长度不能大于256)
  * 功能: 指定页编程
  * 说明: 数据长度禁止跨页输入
----------------------------------------------------------------*/
//static void w25qxx_1page_program(uint32_t addr,uint8_t *pbuf,uint16_t lenth);

/*--------------------------------------------------------------
  * 名称: w25qxx_write(uint32_t addr,uint8_t *pbuf,uint32_t lenth)
  * 传入: addr地址 pbuf数据指针 lenth数据长度(长度不能大于256)
  * 功能: 任意地址和长度写入
  * 说明: 支持任意跨页写入
----------------------------------------------------------------*/
void w25qxx_write(uint32_t addr,uint8_t *pbuf,uint32_t lenth);

/*--------------------------------------------------------------
  * 名称: w25qxx_sector_erase(uint32_t addr)
  * 传入: addr地址
  * 功能: 输入地址对应的扇区(4k)擦除
  * 说明: 需要时间等待busy
----------------------------------------------------------------*/
void w25qxx_sector_erase(uint32_t addr);

/*--------------------------------------------------------------
  * 名称: w25qxx_read_data(uint32_t addr,uint8_t p[],uint32_t lenth);
  * 传入: addr地址 p数据指针 lenth数据长度
  * 功能: 连续读取 数据放到p里
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_read_data(uint32_t addr,uint8_t p[],uint32_t lenth);

/*--------------------------------------------------------------
  * 名称: w25qxx_is_busy(void)
  * 反回: 0空闲 1忙碌中
  * 功能: 检测模块是否忙碌
  * 说明: 
----------------------------------------------------------------*/
uint8_t w25qxx_is_busy(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_wait_free(void)
  * 反回: 0空闲 1忙碌中
  * 功能: 检测模块是否忙碌
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_wait_free(void);

/*--------------------------------------------------------------
  * 名称: w25qxx_init(void)
  * 反回: 无
  * 功能: flash初始化开机
  * 说明: 
----------------------------------------------------------------*/
void w25qxx_init(void);

#endif
