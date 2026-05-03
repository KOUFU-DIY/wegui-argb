#ifndef STM32F103_WE_INPUT_PORT_H
#define STM32F103_WE_INPUT_PORT_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * STM32F103 默认输入端口
 *
 * 这份正式端口头和 Demo/we_input_port_template.h 保持同样的接口风格。
 * 当前工程默认没有接触摸或按键输入，所以给出“安全空实现”：
 * 1. init 不做任何初始化
 * 2. read 永远返回无输入状态
 *
 * 后面如果你接入真实触摸，只需要把这里替换成真实采样逻辑，
 * main.c 的主循环调用顺序不需要再改。
 * -------------------------------------------------------------------------- */

static inline void input_hw_init(void)
{
}

static inline void we_input_port_read(we_indev_data_t *data)
{
    if (data == 0)
    {
        return;
    }

    data->x = 0;
    data->y = 0;
    data->state = WE_TOUCH_STATE_NONE;
}

#endif
