#ifndef WE_PORT_H
#define WE_PORT_H

#if defined(WE_PLATFORM_CMS32C030)
#include "../../CMS32C030/Lcd_Port/cms32c030_hw_config.h"
#elif defined(WE_SIMULATOR)
#include "../../Simulator/we_sim_port_config.h"
#else
#include "stm32f103_hw_config.h"
#endif

#endif
