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

/* =========================
 * 2. 刷新区域像素对齐（可选）
 * =========================
 *
 * WE_LCD_FLUSH_ALIGN_X / WE_LCD_FLUSH_ALIGN_Y（不定义时默认 1 = 不对齐）。
 * 部分屏幕对刷新窗口坐标有硬件粒度要求，核心会在脏矩形入库时把矩形扩张到
 * 对齐边界（Core/dirty_driver.c），扩出的边缘随矩形整块重绘，因此
 * lcd_set_addr 收到的 x0/y0 恒为对齐倍数、x1/y1 恒为对齐倍数-1（含端点）。
 * 约束（we_gui_config.h 编译期校验）：对齐值必须是 2 的幂；SCREEN_WIDTH /
 * SCREEN_HEIGHT 及 PFB 行数（USER_GRAM_NUM / SCREEN_WIDTH）必须是对应
 * 对齐值的整数倍。
 *
 * 【示例 A：QSPI 接口彩屏】
 * 不少 QSPI 屏（含部分 SPI IC 的横竖对齐限制）要求窗口 x/y 起止坐标按
 * 2 或 4 对齐，否则 IC 直接丢弃设窗命令或错位显示。只需：
 *
 *   #define WE_LCD_FLUSH_ALIGN_X (4)
 *   #define WE_LCD_FLUSH_ALIGN_Y (4)
 *
 * set_addr / flush 回调无需任何改动——坐标到达端口前已经对齐。
 *
 * 【示例 B：SSD1306 页式单色 OLED（128x64）】
 * SSD1306 显存按"页"组织：1 页 = 8 行，每字节对应 1 列 x 8 行。y 向必须
 * 按 8 对齐，x 向可逐列寻址：
 *
 *   #define SCREEN_WIDTH  128
 *   #define SCREEN_HEIGHT 64
 *   #define WE_LCD_FLUSH_ALIGN_X (1)
 *   #define WE_LCD_FLUSH_ALIGN_Y (8)
 *   #define USER_GRAM_NUM (SCREEN_WIDTH * 8)   // PFB 行数须为 8 的倍数
 *
 * 端口回调写法要点（伪代码）：
 *
 *   static uint16_t win_x0, win_w, win_row;    // set_addr 记录窗口状态
 *
 *   void ssd1306_set_addr(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
 *   {
 *       // y0/y1 已被核心保证按 8 对齐（y0 % 8 == 0，(y1+1) % 8 == 0）
 *       win_x0 = x0; win_w = x1 - x0 + 1; win_row = 0;
 *       ssd1306_cmd_set_col_range(x0, x1);
 *       ssd1306_cmd_set_page_range(y0 >> 3, y1 >> 3);  // page = y / 8
 *   }
 *
 *   void ssd1306_flush(uint16_t *gram, uint32_t pix_size)
 *   {
 *       // 每收满 8 行就把 RGB565 阈值化并按列打包成 1bpp 页字节发出。
 *       // PFB 行数为 8 的倍数时，每次 flush 块都是整页，无跨块残页。
 *       uint16_t rows = pix_size / win_w;
 *       for (uint16_t page_row = 0; page_row < rows; page_row += 8) {
 *           for (uint16_t col = 0; col < win_w; col++) {
 *               uint8_t b = 0;
 *               for (uint8_t bit = 0; bit < 8; bit++) {
 *                   uint16_t px = gram[(page_row + bit) * win_w + col];
 *                   if (px != 0) b |= (uint8_t)(1u << bit);  // 亮度阈值化
 *               }
 *               ssd1306_data(b);
 *           }
 *       }
 *       win_row += rows;
 *   }
 */

#endif
