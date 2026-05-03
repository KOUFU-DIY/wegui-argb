#ifndef WE_INPUT_PORT_TEMPLATE_H
#define WE_INPUT_PORT_TEMPLATE_H

#include <stdint.h>
#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * WeGui 输入端口移植模板
 *
 * 作用说明：
 * 1. 这份文件不是当前工程实际参与编译的正式配置头
 * 2. 它只作为“移植输入设备时”的参考模板
 * 3. 适合触摸屏、按键、编码器等输入链路做第一版骨架
 *
 * 使用步骤建议：
 * 1. 先根据你的硬件决定输入来源：触摸 / 按键 / 编码器
 * 2. 在 Demo/we_input_port_template.c 中补齐初始化和读取逻辑
 * 3. 每次得到新的输入状态后，填充 we_indev_data_t
 * 4. 再调用 we_gui_indev_handler(lcd, &data) 送进 GUI 内核
 * -------------------------------------------------------------------------- */

/**
 * @brief 输入硬件初始化模板接口。
 * @return 无。
 */
void input_hw_init(void);

/**
 * @brief 读取当前输入状态并写入输入数据结构。
 * @param data 输入状态输出结构体指针。
 * @return 无。
 */
void we_input_port_read(we_indev_data_t *data);

#endif
