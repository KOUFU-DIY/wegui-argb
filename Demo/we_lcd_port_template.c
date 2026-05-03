#include "we_lcd_port_template.h"

/* --------------------------------------------------------------------------
 * WeGui LCD 端口移植模板实现
 *
 * 这份 .c 文件和 Demo/we_lcd_port_template.h 配套使用，作用是：
 * 1. 给新平台移植时提供“最小可改骨架”
 * 2. 把必须由用户自己适配的接口明确列出来
 * 3. 未适配时直接 while(1) 停住，避免静默失败
 *
 * 使用建议：
 * 1. 真正移植时，把这份文件复制到你的平台目录
 * 2. 然后逐个把下面的占位函数替换成真实硬件实现
 * 3. 哪个接口还没适配，程序就会停在对应位置，方便排查
 * -------------------------------------------------------------------------- */

/* =========================
 * 1. LCD 通信端口接口
 * =========================
 *
 * 这些函数通常由底层端口文件提供，例如：
 * 1. SPI/IIC 初始化
 * 2. 把 RGB565 / RGB888 像素流真正发到屏幕
 */

/**
 * @brief 初始化 LCD 底层硬件链路。
 * @return 无。
 */
void lcd_hw_init(void)
{
    while (1)
    {
    }
}

/**
 * @brief 通过底层端口输出 RGB565 像素流。
 * @param gram RGB565 像素缓冲区指针。
 * @param pix_size 本次输出像素数量。
 * @return 无。
 */
void we_lcd_rgb565_port(uint16_t *gram, uint32_t pix_size)
{
    (void)gram;
    (void)pix_size;
    while (1)
    {
    }
}

/**
 * @brief 通过底层端口输出 RGB888 像素流。
 * @param gram RGB888 像素缓冲区指针。
 * @param pix_size 本次输出像素数量。
 * @return 无。
 */
void we_lcd_rgb888_port(uint8_t *gram, uint32_t pix_size)
{
    (void)gram;
    (void)pix_size;
    while (1)
    {
    }
}

/* 说明：
 * 1. 这份模板不再额外提供单独的 lcd_ic_init / set_addr / set_bright 示例函数
 * 2. 建议你直接在自己的显示端口实现链里，把屏幕初始化、设窗和推屏整合进去
 * 3. 如果某一步还没适配，就保留 while(1) 占位，便于上电后快速定位 */
