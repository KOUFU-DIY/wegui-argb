#ifndef WE_SIM_PORT_CONFIG_H
#define WE_SIM_PORT_CONFIG_H

#include <stdint.h>
#include "we_user_config.h" /* 先取 LCD_DEEP/DEEP_* 等用户配置：本头可能先于内核头被包含 */

/* --------------------------------------------------------------------------
 * SimLite 轻量模拟器显示端口配置（纯 Win32/GDI，无 SDL 依赖）
 *
 * we_hw_port.h 的 WE_SIMULATOR 分支路由到本头；SimLite 构建把仓库根
 * 与本目录加入 include 路径即可复用整个内核路由。
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化 GDI 帧缓冲窗口（--shot 自检模式下只建缓冲不开窗）
 */
void lite_lcd_init(void);

/**
 * @brief 设置模拟 LCD 写入窗口地址
 * @param x0 起始 X 坐标
 * @param y0 起始 Y 坐标
 * @param x1 结束 X 坐标
 * @param y1 结束 Y 坐标
 */
void lite_lcd_set_addr(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief 初始化显示硬件抽象层（SimLite 实现）
 */
void lcd_hw_init(void);

/**
 * @brief 打开背光（空实现）
 */
void lcd_bl_on(void);

/**
 * @brief 关闭背光（空实现）
 */
void lcd_bl_off(void);

/**
 * @brief 毫秒级延时
 * @param ms 延时毫秒数
 */
void lcd_delay_ms(uint32_t ms);

/**
 * @brief 输出 RGB565 像素块（写入帧缓冲，统一由 lite_present 刷窗）
 * @param gram 像素数据指针
 * @param pix_size 像素数量
 */
void lcd_rgb565_port(uint16_t *gram, uint32_t pix_size);

/**
 * @brief 输出 GRAY8 像素块（灰度→ARGB 复制写入帧缓冲）
 * @param gram 像素数据指针（1 字节亮度/像素）
 * @param pix_size 像素数量
 * @note 定义 WE_SIM_MONO_PREVIEW=1（默认跟随 WE_CFG_MONO_STYLE）时按
 *       WE_SIM_MONO_THRESHOLD（默认 128）阈值二值显示，模拟单色 OLED 观感
 */
void lcd_gray8_port(uint8_t *gram, uint32_t pix_size);

/* 屏幕尺寸与 GRAM 配置统一由 we_user_config.h 的 WE_SIMULATOR 分支定义，
 * 此处不重复（本头会先于用户配置被包含，重复定义会产生覆盖告警）。 */

/* 脏矩形/调试/裁剪类 WE_CFG_* 一律交给 we_user_config.h 统一决定，
 * 这里不再重复定义（本头先于内核头包含，重复定义会产生覆盖告警）。 */

#ifndef SCREEN_X_OFFSET
#define SCREEN_X_OFFSET 0
#endif
#ifndef SCREEN_Y_OFFSET
#define SCREEN_Y_OFFSET 0
#endif

#define lcd_ic_init()                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        lite_lcd_init();                                                                                               \
    } while (0)



/* GDI 模拟器的 flush 是同步内存拷贝，无异步发送，双缓冲无收益。 */
#define WE_PORT_FLUSH_ASYNC (0)

#endif
