#ifndef WE_LCD_PORT_TEMPLATE_H
#define WE_LCD_PORT_TEMPLATE_H

#include <stdint.h>

/* --------------------------------------------------------------------------
 * WeGui LCD 端口移植模板
 *
 * 作用说明：
 * 1. 这份文件不是当前正式工程里参与编译的配置头。
 * 2. 它只作为移植到新平台、新屏幕时的参考模板使用。
 * 3. 后续如果要移植到别的 MCU，建议复制一份后再改名使用，
 *    例如：stm32f407_we_port.h / ch32v307_we_port.h / esp32_we_port.h。
 * 4. 与它配套的占位实现放在 Demo/we_lcd_port_template.c。
 *
 * 使用建议：
 * 1. 先根据目标屏幕修改分辨率、GRAM 分块和偏移参数。
 * 2. 再选择 LCD IC 和通信端口。
 * 3. 把 Demo/we_lcd_port_template.c 里的占位接口改成你的硬件实现。
 * 4. 屏幕初始化、设窗和推屏逻辑，建议直接落到
 *    we_lcd_rgb565_port / we_lcd_rgb888_port 对应的底层链路里完成。
 * 5. 最后把 lcd_ic_init / lcd_set_addr / lcd_set_bright 这些宏，
 *    对接到你自己的底层驱动函数。
 *
 * 注意：
 * 1. Core 层通过 we_user_config.h 间接选择平台端口头。
 * 2. 所以这份模板本身不应该被 Core 直接 include。
 * 3. 真正投入使用时，请复制成正式平台文件后，再在 we_user_config.h 中接入。
 * -------------------------------------------------------------------------- */

/* =========================
 * 0. 需要用户适配的通用接口
 * =========================
 *
 * 说明：
 * 1. 这些接口只是模板声明，不参与当前正式工程编译。
 * 2. 真正移植时，请在对应平台目录里实现它们。
 */

/**
 * @brief LCD 底层硬件初始化模板接口。
 * @return 无。
 */
void lcd_hw_init(void);

/**
 * @brief 通过底层端口输出 RGB565 像素流。
 * @param gram RGB565 像素缓冲区指针。
 * @param pix_size 本次输出像素数量。
 * @return 无。
 */
void we_lcd_rgb565_port(uint16_t *gram, uint32_t pix_size);

/**
 * @brief 通过底层端口输出 RGB888 像素流。
 * @param gram RGB888 像素缓冲区指针。
 * @param pix_size 本次输出像素数量。
 * @return 无。
 */
void we_lcd_rgb888_port(uint8_t *gram, uint32_t pix_size);

/* =========================
 * 1. 屏幕与 PFB 基本参数
 * =========================
 *
 * 这些参数决定：
 * 1. GUI 可见分辨率
 * 2. PFB 每次刷新的切片大小
 * 3. RAM 占用和单次推屏像素数量
 */
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

#define SCREEN_X_OFFSET 0
#define SCREEN_Y_OFFSET 0

/* 是否打开 DMA 双缓冲模式 */
#define GRAM_DMA_BUFF_EN (1)

/* PFB 缓冲像素数量，最小建议为 SCREEN_WIDTH * 1 */
#define GRAM_NUM (SCREEN_WIDTH * 2)

#endif
