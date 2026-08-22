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
 * 触摸链路（轮询式）使用步骤：
 * 1. 在 we_input_port_read 中读取硬件，填充 we_indev_data_t
 *    （x/y 坐标 + state = PRESSED/STAY/RELEASED/NONE）
 * 2. 注册进内核：WE_CFG_ENABLE_INPUT_PORT_BIND=1 时把它作为
 *    input_cb 传给 we_gui_init(...)；=0 时内核直接按
 *    we_input_port_read 函数名链接，无需注册
 * 3. we_gui_task_handler() 每周期自动轮询并分发；只有自行管理
 *    输入状态机时才需要手动调 we_gui_indev_handler
 *
 * 按键链路（注入式，内核不轮询按键）：
 * 1. 端口自行消抖，把物理按键翻译成语义键
 *    WE_KEY_UP/DOWN/LEFT/RIGHT/PREV/NEXT/OK/BACK
 * 2. 双沿端口调 we_gui_key_press / we_gui_key_release；OK 键必须
 *    上报松开沿（按住期间才有按压视觉），方向键的自动重复由端口
 *    重复注入按下沿
 * 3. 简单端口调 we_gui_key_inject 一次注入完整一击
 * 4. 三个注入函数 ISR 安全（SPSC 队列），可在中断里直接调用；
 *    参考实现：SimLite/fenster_port.c、AD14N/Key_Port
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
