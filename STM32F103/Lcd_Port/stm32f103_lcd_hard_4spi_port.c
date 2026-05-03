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
#include "we_gui_driver.h"

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

#if ((LCD_DEEP == DEEP_OLED) || (LCD_DEEP == DEEP_GRAY8))
    /* OLED: SCL 默认高，第二个跳变沿有效 */
    spi.SPI_CPOL = SPI_CPOL_High;
    spi.SPI_CPHA = SPI_CPHA_2Edge;
#elif ((LCD_DEEP == DEEP_RGB111) || (LCD_DEEP == DEEP_RGB332) || \
       (LCD_DEEP == DEEP_RGB444) || (LCD_DEEP == DEEP_RGB565) || \
       (LCD_DEEP == DEEP_RGB888))
    /* TFT: SCL 默认低，第一个跳变沿有效 */
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;
#endif

    spi.SPI_NSS              = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4; /* 低速初始化屏幕 */
    spi.SPI_FirstBit         = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial    = 7;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPIx, ENABLE);
    RCC_PCLK2Config(RCC_HCLK_Div1);
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
#if ((LCD_DEEP == DEEP_OLED) || (LCD_DEEP == DEEP_GRAY8))
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

#elif ((LCD_DEEP == DEEP_RGB111) || (LCD_DEEP == DEEP_RGB332) || \
       (LCD_DEEP == DEEP_RGB444) || (LCD_DEEP == DEEP_RGB565) || \
       (LCD_DEEP == DEEP_RGB888))
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

#else
    while (1)
    {
    }
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

/* 发送 num 个数据字节 */
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

/* OLED 刷屏接口 (预留, 按需实现) */
void lcd_oled_port(uint16_t x0, uint16_t x1, uint16_t page, uint8_t *page_gram)
{
    (void)x0;
    (void)x1;
    (void)page;
    (void)page_gram;
}

/* 灰度 OLED 刷屏接口 (预留, 按需实现) */
void lcd_gray_port(uint16_t x0, uint16_t x1, uint16_t page, uint8_t *page_gram)
{
    (void)x0;
    (void)x1;
    (void)page;
    (void)page_gram;
}

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
    uint32_t pixel_count;
    uint8_t  r, g, b;
    uint16_t rgb565;

    if (gram == NULL)
    {
        return;
    }

    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    spi_set_16b();
    LCD_DC_Set();
    LCD_CS_Clr();

    /* pix_size 为 RGB888 字节数，每 3 字节转 1 个 RGB565 */
    pixel_count = pix_size / 3U;
    while (pixel_count--)
    {
        r = *gram++;
        g = *gram++;
        b = *gram++;
        rgb565 = (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                            ((uint16_t)(g & 0xFCU) << 3) |
                            ((uint16_t)b >> 3));
        wait_lcd_spi_txtemp_free();
        send_lcd_spi_dat(rgb565);
    }

    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
    spi_set_8b();
}

#endif
