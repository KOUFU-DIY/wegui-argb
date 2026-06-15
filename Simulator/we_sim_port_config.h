#ifndef WE_SIM_PORT_CONFIG_H
#define WE_SIM_PORT_CONFIG_H

#include <stdint.h>

/* --------------------------------------------------------------------------
 * 模拟器默认显示端口配置
 *
 * 这一层只负责给 Core 提供屏幕尺寸、GRAM 配置和显示端口绑定。
 * 这里不要直接包含 sdl_port.h，否则会在 GUI 核心类型尚未定义时形成循环依赖。
 * -------------------------------------------------------------------------- */

/* 模拟器分辨率在 we_user_config.h 的 WE_SIMULATOR 分支统一定义
 * （此头只被 IC 驱动 TU 包含，在这里覆盖会造成编译单元间不一致）。 */

/**
 * @brief 初始化模拟 LCD 资源
 */
void sim_lcd_init(void);

/**
 * @brief 设置模拟 LCD 写入窗口地址
 * @param x0 起始 X 坐标
 * @param y0 起始 Y 坐标
 * @param x1 结束 X 坐标
 * @param y1 结束 Y 坐标
 */
void sim_lcd_set_addr(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief 初始化显示硬件抽象层（模拟器实现）
 */
void lcd_hw_init(void);

/**
 * @brief 打开背光（模拟器为空实现）
 */
void lcd_bl_on(void);

/**
 * @brief 关闭背光（模拟器为空实现）
 */
void lcd_bl_off(void);

/**
 * @brief 毫秒级延时
 * @param ms 延时毫秒数
 */
void lcd_delay_ms(uint32_t ms);

/**
 * @brief 输出 RGB565 像素块
 * @param gram 像素数据指针
 * @param pix_size 像素数量
 */
void lcd_rgb565_port(uint16_t *gram, uint32_t pix_size);

/**
 * @brief 输出 RGB888 像素块
 * @param gram 像素数据指针
 * @param pix_size 字节数量（3 字节/像素）
 */
void lcd_rgb888_port(uint8_t *gram, uint32_t pix_size);

/**
 * @brief LCD 地址窗口设置端口转发函数
 * @param x0 起始 X 坐标
 * @param y0 起始 Y 坐标
 * @param x1 结束 X 坐标
 * @param y1 结束 Y 坐标
 */
static inline void we_lcd_set_addr_port(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    sim_lcd_set_addr(x0, y0, x1, y1);
}

#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 280
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 240
#endif

#ifndef GRAM_DMA_BUFF_EN
#define GRAM_DMA_BUFF_EN (1)
#endif
#ifndef USER_GRAM_NUM
#define USER_GRAM_NUM (SCREEN_WIDTH * 2)
#endif

#ifndef GRAM_NUM
#define GRAM_NUM (USER_GRAM_NUM)
#endif

#ifndef LCD_DEEP
#define LCD_DEEP (DEEP_RGB565)
#endif

#ifndef WE_CFG_DIRTY_STRATEGY
#define WE_CFG_DIRTY_STRATEGY (2)
#endif

#ifndef WE_CFG_DIRTY_MAX_NUM
#define WE_CFG_DIRTY_MAX_NUM (8)
#endif

#ifndef WE_CFG_DEBUG_DIRTY_RECT
#define WE_CFG_DEBUG_DIRTY_RECT (0)
#endif

/* 控件性能压测开关
 * 0: 关闭，正常按需重绘
 * 1: 打开，每帧强制标脏所有控件，持续全量重绘以压测控件渲染性能 */
#ifndef WE_CFG_DEBUG_PERF_STRESS
#define WE_CFG_DEBUG_PERF_STRESS (0)
#endif

#ifndef WE_CFG_ENABLE_INDEXED_QOI
#define WE_CFG_ENABLE_INDEXED_QOI (1)
#endif

#ifndef WE_CFG_GUI_TASK_MAX_NUM
#define WE_CFG_GUI_TASK_MAX_NUM (4)
#endif

#ifndef WE_CFG_ENABLE_INPUT_PORT_BIND
#define WE_CFG_ENABLE_INPUT_PORT_BIND (0)
#endif

#ifndef WE_CFG_ENABLE_STORAGE_PORT_BIND
#define WE_CFG_ENABLE_STORAGE_PORT_BIND (0)
#endif

#ifndef SCREEN_X_OFFSET
#define SCREEN_X_OFFSET 0
#endif
#ifndef SCREEN_Y_OFFSET
#define SCREEN_Y_OFFSET 0
#endif

#define lcd_ic_init()                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        sim_lcd_init();                                                                                                \
    } while (0)

#define lcd_set_addr sim_lcd_set_addr

#if (LCD_DEEP == DEEP_RGB565)
#define LCD_FLUSH_PORT lcd_rgb565_port
#elif (LCD_DEEP == DEEP_RGB888)
#define LCD_FLUSH_PORT lcd_rgb888_port
#endif

/* SDL 模拟器的 flush 是同步拷贝，无异步发送，双缓冲无收益。 */
#define WE_PORT_FLUSH_ASYNC (0)

#endif
