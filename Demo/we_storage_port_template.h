#ifndef WE_STORAGE_PORT_TEMPLATE_H
#define WE_STORAGE_PORT_TEMPLATE_H

#include <stdint.h>

/* --------------------------------------------------------------------------
 * WeGui 外部存储端口移植模板
 *
 * 作用说明：
 * 1. 这份文件不是当前工程实际参与编译的正式配置头
 * 2. 它只作为“外挂 Flash / 外部资源读取”移植时的参考模板
 * 3. 适合资源分离存储、字库外置、图片外置这类场景
 *
 * 使用步骤建议：
 * 1. 如果项目不需要外部存储，可以直接忽略这套模板
 * 2. 如果后面要接 SPI Flash / NOR / NAND / EEPROM，再补这里
 * 3. 核心是给出“初始化 + 按地址读取”这两个最小骨架
 * -------------------------------------------------------------------------- */

/**
 * @brief 外部存储硬件初始化模板接口。
 * @return 无。
 */
void storage_hw_init(void);

/**
 * @brief 从外部存储按地址读取数据。
 * @param buf 读取结果缓冲区。
 * @param addr 外部存储起始地址。
 * @param len 读取字节长度。
 * @return 读取结果，1 表示成功，0 表示失败。
 */
uint8_t we_storage_port_read(uint8_t *buf, uint32_t addr, uint32_t len);

#endif
