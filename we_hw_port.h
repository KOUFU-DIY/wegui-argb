#ifndef WE_PORT_H
#define WE_PORT_H

#if defined(WE_PLATFORM_CMS32C030)
#include "../../CMS32C030/Lcd_Port/cms32c030_hw_config.h"
#elif defined(WE_SIMULATOR)
#include "../../Simulator/we_sim_port_config.h"
#elif defined(WE_PLATFORM_CW32L012)
#include "../../CW32L012/Lcd_Port/cw32l012_hw_config.h"
#elif defined(WE_PLATFORM_AD15N)
#include "ad15n_hw_config.h"
#else
#include "stm32f103_hw_config.h"
#endif

#endif
