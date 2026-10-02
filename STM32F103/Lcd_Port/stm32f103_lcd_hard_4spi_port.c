/*
 * Copyright 2025 Lu Zhihao
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "stm32f103_hw_config.h"

#if (LCD_PORT == _HARD_4SPI)
#include "stm32f103_lcd_hard_4spi_port.h"
#include "wegui.h"

/*
 * 使用 static __inline 而非裸 inline：
 * AC5 在不同优化等级下对裸 inline 的外部符号生成策略不稳定，
 * O1 下可能链接失败。static __inline 语义明确，不对外导出符号。
 */
static __inline void spi_set_8b(void)
{
    LCD_SPIx->CR1 = LCD_SPIx->CR1 & (uint16_t)~SPI_DataSize_16b;
}

static __inline void spi_set_16b(void)
{
    LCD_SPIx->CR1 = LCD_SPIx->CR1 | SPI_DataSize_16b;
}

/* 软件延时，无需精准 */
void lcd_delay_ms(volatile uint32_t ms)
{
    volatile uint32_t i;
    while (ms--)
    {
        i = 10000U;
        while (i--)
        {
        }
    }
}

void lcd_bl_on(void)
{
    LCD_BL_Set();
}

void lcd_bl_off(void)
{
    LCD_BL_Clr();
}

/* 纯硬件 SPI 无 DMA，接口始终空闲 */
uint8_t lcd_is_busy(void)
{
    return 0;
}

void lcd_hw_init(void)
{
    SPI_InitTypeDef spi;

    LCD_SCL_IO_Init();
    LCD_SDA_IO_Init();
    LCD_RES_IO_Init();
    LCD_DC_IO_Init();
    LCD_CS_IO_Init();
    LCD_CS_Set();
    LCD_BL_IO_Init();
    lcd_bl_off();

    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode      = SPI_Mode_Master;
    spi.SPI_DataSize  = SPI_DataSize_8b;

#if ((LCD_TYPE == HW_LCD_OLED) || (LCD_TYPE == HW_LCD_GRAY))
    /* OLED: SCL 默认高，第二个跳变沿有效 */
    spi.SPI_CPOL = SPI_CPOL_High;
    spi.SPI_CPHA = SPI_CPHA_2Edge;
#else
    /* TFT: SCL 默认低，第一个跳变沿有效 */
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;
#endif

    spi.SPI_NSS              = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = LCD_SPI_BaudRatePrescaler_x; /* hw_config 3.2 节配置 */
    spi.SPI_FirstBit         = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial    = 7;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPIx, ENABLE);
    RCC_PCLK2Config(RCC_HCLK_Divx); /* hw_config 3.2 节配置 */
    SPI_Init(LCD_SPIx, &spi);
    SPI_Cmd(LCD_SPIx, ENABLE);

    LCD_RES_Clr();
    lcd_delay_ms(100);
    LCD_RES_Set();
    lcd_delay_ms(100);

    lcd_ic_init();
}

/* 发送 1 个命令字节 (DC=0) */
void lcd_send_1Cmd(uint8_t dat)
{
    send_lcd_spi_done();
    LCD_DC_Clr();
    LCD_CS_Clr();
    send_lcd_spi_dat(dat);
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
}

/* 发送命令帧：p[0] 为命令，p[1..] 为跟随数据 */
void lcd_send_nCmd(uint8_t *p, uint16_t num)
{
#if ((LCD_TYPE == HW_LCD_OLED) || (LCD_TYPE == HW_LCD_GRAY))
    uint16_t i = 0;
    send_lcd_spi_done();
    LCD_DC_Clr();
    LCD_CS_Clr();
    while (num > i)
    {
        wait_lcd_spi_txtemp_free();
        send_lcd_spi_dat(p[i++]);
    }
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();

#else
    send_lcd_spi_done();
    spi_set_8b();

    /* p[0]: 命令 */
    LCD_DC_Clr();
    LCD_CS_Clr();
    send_lcd_spi_dat(p[0]);
    num--;
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();

    /* p[1..]: 数据 */
    LCD_DC_Set();
    while (num-- > 0)
    {
        p++;
        wait_lcd_spi_txtemp_free();
        send_lcd_spi_dat(*p);
    }
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
#endif
}

/* 发送 1 个数据字节 (DC=1) */
void lcd_send_1Dat(uint8_t dat)
{
    send_lcd_spi_done();
    LCD_DC_Set();
    LCD_CS_Clr();
    send_lcd_spi_dat(dat);
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
}

/* 发送 num 个数据字节 (轮询) */
void lcd_send_nDat(uint8_t *p, uint16_t num)
{
    uint16_t i = 0;
    send_lcd_spi_done();
    LCD_DC_Set();
    LCD_CS_Clr();
    while (num > i)
    {
        wait_lcd_spi_txtemp_free();
        send_lcd_spi_dat(p[i++]);
    }
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
}

#if ((LCD_TYPE == HW_LCD_OLED) || (LCD_TYPE == HW_LCD_GRAY))

/* ---- OLED / 灰度屏共用刷新窗口状态 ----
 * 页式单色 OLED 无硬件刷新窗口，靠这里的游标逐页寻址；
 * 灰度 OLED（SSD1327 类）有硬件窗口自增，lcd_set_windows 转发真窗口
 * 命令，端口只留窗口宽度供打包使用。 */

static uint16_t lcd_win_x0;
static uint16_t lcd_win_x1;
#if (LCD_TYPE == HW_LCD_OLED)
static uint16_t lcd_win_cur_y; /* 页游标：页式 IC 无硬件窗口，跨 flush 切片续传用 */
#endif

/* 记录本轮刷新窗口（内核契约：每个脏矩形调用一次，随后按 PFB 切片连续 flush） */
void lcd_set_windows(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_win_x0 = x0;
    lcd_win_x1 = x1;
#if (LCD_TYPE == HW_LCD_GRAY)
    gray_set_addr(x0, y0, x1, y1); /* 灰度 IC 有硬件窗口，转发真窗口命令 */
    (void)y0;
#else
    lcd_win_cur_y = y0;
    (void)y1;
#endif
}

#endif /* OLED || GRAY */

#if (LCD_TYPE == HW_LCD_OLED)

/* ---- 页式单色 OLED 刷屏 ----
 * lcd_oled_port 按整页切片逐页 oled_set_addr 寻址后，阈值二值化 +
 * 垂直 8 像素打包成页字节，边转换边发送（无中间行缓冲）。
 * 依赖 WE_LCD_FLUSH_ALIGN_Y = 8（脏矩形与 PFB 切片整页对齐）。 */

/* GRAY8 切片 -> 页字节流（行主序，宽 = 窗口宽，1 字节亮度/像素） */
void lcd_oled_port(uint8_t *gram, uint32_t pix_size)
{
    uint16_t width = (uint16_t)(lcd_win_x1 - lcd_win_x0 + 1U);
    uint16_t rows, pages, pg, x;

    if ((gram == 0) || (width == 0U))
        return;

    rows = (uint16_t)(pix_size / width);
    pages = (uint16_t)(rows >> 3);

    for (pg = 0; pg < pages; pg++)
    {
        uint8_t *base = gram + ((uint32_t)pg * 8U * width);

        oled_set_addr((uint8_t)((lcd_win_cur_y >> 3) + pg), (uint8_t)lcd_win_x0);

        send_lcd_spi_done();
        LCD_DC_Set();
        LCD_CS_Clr();
        for (x = 0; x < width; x++)
        {
            uint8_t byte = 0U;
            uint8_t bit;

            for (bit = 0U; bit < 8U; bit++)
            {
                if (base[(uint32_t)bit * width + x] >= WE_OLED_GRAY_THRESHOLD)
                    byte |= (uint8_t)(1U << bit);
            }
            wait_lcd_spi_txtemp_free();
            send_lcd_spi_dat(byte);
        }
        wait_lcd_spi_txtemp_free();
        send_lcd_spi_done();
        LCD_CS_Set();
    }

    lcd_win_cur_y = (uint16_t)(lcd_win_cur_y + rows);
}

#endif /* LCD_TYPE == HW_LCD_OLED */

#if (LCD_TYPE == HW_LCD_GRAY)

/* ---- 灰度 OLED（SSD1327 类）刷屏：GRAY8 -> 4bpp 双像素打包 ----
 * 窗口寻址由 lcd_set_windows 转发，IC 窗口内自动递增，跨 flush 切片连续；
 * 每字节 2 像素：高 4 位 = 左像素（与屏厂例程一致），ALIGN_X=2 保证偶数宽。 */
void lcd_gray_port(uint8_t *gram, uint32_t pix_size)
{
    uint16_t width = (uint16_t)(lcd_win_x1 - lcd_win_x0 + 1U);
    uint16_t rows, row, pairs, x;

    if ((gram == 0) || (width == 0U))
        return;

    rows  = (uint16_t)(pix_size / width);
    pairs = (uint16_t)(width >> 1);

    send_lcd_spi_done();
    LCD_DC_Set();
    LCD_CS_Clr();
    for (row = 0; row < rows; row++)
    {
        uint8_t *src = gram + ((uint32_t)row * width);

        for (x = 0; x < pairs; x++)
        {
            wait_lcd_spi_txtemp_free();
            send_lcd_spi_dat((uint8_t)((src[2U * x] & 0xF0U) | (src[2U * x + 1U] >> 4)));
        }
    }
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
}

#endif /* LCD_TYPE == HW_LCD_GRAY */

/* RGB565 刷屏: 16 位 SPI 推流 */
void lcd_rgb565_port(uint16_t *gram, uint32_t pix_size)
{
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    spi_set_16b();
    LCD_DC_Set();
    LCD_CS_Clr();
    while (pix_size--)
    {
        wait_lcd_spi_txtemp_free();
        send_lcd_spi_dat(*gram++);
    }
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
    spi_set_8b();
}

/* RGB888 刷屏: 逐像素转 RGB565 后推流 */
void lcd_rgb888_port(uint8_t *gram, uint32_t pix_size)
{
    if (gram == NULL)
    {
        return;
    }

    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    spi_set_8b();
    LCD_DC_Set();
    LCD_CS_Clr();

    /* 内核按像素数传入，RGB888 每像素 R/G/B 3 字节原样直出 */
    pix_size *= 3U;
    while (pix_size--)
    {
        wait_lcd_spi_txtemp_free();
        send_lcd_spi_dat(*gram++);
    }

    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
}

#endif
