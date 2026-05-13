#ifndef STM32F030_WE_INPUT_PORT_H
#define STM32F030_WE_INPUT_PORT_H

#include "we_gui_driver.h"

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
