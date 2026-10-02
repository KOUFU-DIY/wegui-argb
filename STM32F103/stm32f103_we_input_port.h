#ifndef STM32F103_WE_INPUT_PORT_H
#define STM32F103_WE_INPUT_PORT_H

#include "wegui.h"

/* --------------------------------------------------------------------------
 * STM32F103 默认输入端口
 *
 * 这份正式端口头和 Demo/we_input_port_template.h 保持同样的接口风格。
 * 当前工程默认没有接触摸，main.c 端口描述的 touch_read 填 NULL，
 * init 不做任何初始化。
 *
 * 后面如果你接入真实触摸，在这里实现采样函数
 * void touch_read(we_touch_data_t *data)（按下填坐标并置 pressed = 1，
 * 松开置 0），再填进 main.c 端口描述的 touch_read，主循环不需要改。
 * -------------------------------------------------------------------------- */

static inline void input_hw_init(void)
{
}

#endif
