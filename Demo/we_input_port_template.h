#ifndef WE_INPUT_PORT_TEMPLATE_H
#define WE_INPUT_PORT_TEMPLATE_H

#include <stdint.h>
#include "wegui.h"

/* --------------------------------------------------------------------------
 * WeGui 输入端口移植模板
 *
 * 作用说明：
 * 1. 这份文件不是当前工程实际参与编译的正式配置头
 * 2. 它只作为“移植输入设备时”的参考模板
 * 3. 适合触摸屏、按键、编码器等输入链路做第一版骨架
 *
 * 触摸链路（轮询式）使用步骤：
 * 1. 在 we_touch_port_read 中读取硬件，填充 we_touch_data_t：
 *    按下时填 x/y 坐标并置 pressed = 1，未按下置 pressed = 0
 *    （可不填坐标）；按下/按住/松开的边沿由内核判定
 * 2. 把它填进端口描述 we_port_t 的 touch_read（无触摸填 NULL）
 * 3. we_gui_run() 每次调用自动采样并分发（采样间隔见
 *    WE_CFG_INDEV_PERIOD_MS）；只有自行管理触摸边沿（脚本回放等）
 *    时才需要直接调 we_gui_indev_handler
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
 * @brief 采样当前触摸状态。
 * @param data 传出：是否按下与坐标（内核调用前已预填上一次的坐标）。
 * @return 无。
 */
void we_touch_port_read(we_touch_data_t *data);

#endif
