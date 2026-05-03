#ifndef WE_GUI_CONFIG_H
#define WE_GUI_CONFIG_H

#include "stdint.h"

#define DEEP_RGB565 (4) /* RGB565 */
#define DEEP_RGB888 (5) /* RGB888 */

/* 平台端口头必须显式给出以下配置。
 * Core 层不再提供默认值，避免默认值和平台值混用后产生重复定义或歧义。 */

#ifndef LCD_DEEP
#error "LCD_DEEP must be defined by platform port config.Like DEEP_RGB565"
#endif

#ifndef SCREEN_WIDTH
#error "SCREEN_WIDTH must be defined by platform port config."
#endif

#ifndef SCREEN_HEIGHT
#error "SCREEN_HEIGHT must be defined by platform port config."
#endif

#ifndef GRAM_DMA_BUFF_EN
#error "GRAM_DMA_BUFF_EN must be defined by platform port config."
#endif

#ifndef WE_CFG_DIRTY_STRATEGY
#error "WE_CFG_DIRTY_STRATEGY must be defined by platform port config."
#endif

#ifndef WE_CFG_DIRTY_MAX_NUM
#error "WE_CFG_DIRTY_MAX_NUM must be defined by platform port config."
#endif

#ifndef WE_CFG_DEBUG_DIRTY_RECT
#error "WE_CFG_DEBUG_DIRTY_RECT must be defined by platform port config."
#endif

#ifndef WE_CFG_ENABLE_INDEXED_QOI
#error "WE_CFG_ENABLE_INDEXED_QOI must be defined by platform port config."
#endif

#ifndef WE_CFG_GUI_TASK_MAX_NUM
#error "WE_CFG_GUI_TASK_MAX_NUM must be defined by platform port config."
#endif

#ifndef WE_CFG_GUI_TIMER_MAX_NUM
#error "WE_CFG_GUI_TIMER_MAX_NUM must be defined by platform port config."
#endif

#ifndef WE_CFG_ENABLE_INPUT_PORT_BIND
#error "WE_CFG_ENABLE_INPUT_PORT_BIND must be defined by platform port config."
#endif

#ifndef WE_CFG_ENABLE_STORAGE_PORT_BIND
#error "WE_CFG_ENABLE_STORAGE_PORT_BIND must be defined by platform port config."
#endif

/* 滑动手势识别阈值 (像素)，位移超过此值才判定为滑动。
 * 平台端口可自行 #define 覆盖，不定义则使用默认值。 */
#ifndef WE_CFG_SWIPE_THRESHOLD
#define WE_CFG_SWIPE_THRESHOLD 30
#endif

#endif
