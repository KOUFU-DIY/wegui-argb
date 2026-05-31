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

/* 渲染性能统计开关。
 * 1：保留 stat_render_frames / stat_pfb_pushes / stat_pushed_pixels 三个计数器，
 *    可用于 FPS 显示和真机性能分析；
 * 0：从 we_lcd_t 中去掉这 3 个 uint32 字段（每实例省 12 字节 RAM），
 *    并消除推屏热路径里的累加开销，适合对 RAM 敏感的量产固件。
 * 平台端口不定义时默认启用，保证 FPS demo 等开箱即用。 */
#ifndef WE_CFG_ENABLE_RENDER_STATS
#define WE_CFG_ENABLE_RENDER_STATS 1
#endif

/* 控件性能压测开关。
 * 1：每帧强制把所有顶层控件按其脏矩形重新标脏，使 GUI 持续全量重绘，
 *    配合 FPS / stat 计数器即可测出当前页面控件的极限渲染性能；
 * 0：正常按需重绘（仅有内容变化的控件才会刷新）。
 * 仅用于性能调试，量产固件请保持 0。
 * 注意：开启后帧率会明显下降，这是预期现象（衡量的是最坏情况吞吐）。 */
#ifndef WE_CFG_DEBUG_PERF_STRESS
#define WE_CFG_DEBUG_PERF_STRESS 0
#endif

#endif
