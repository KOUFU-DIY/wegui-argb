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
 * See the License for the specific language governing permissionas and
 * limitations under the License.
 */

#include "stm32f030_hw_config.h"

#if (LCD_PORT == _HARD_4SPI)
#include "stm32f030_lcd_hard_4spi_port.h"
#include "we_gui_driver.h"

static __inline void spi_set_8b(void)
{
    SPI_DataSizeConfig(LCD_SPIx, SPI_DataSize_8b);
}

static __inline void spi_set_16b(void)
{
    SPI_DataSizeConfig(LCD_SPIx, SPI_DataSize_16b);
}

void lcd_delay_ms(volatile uint32_t ms)
{
    volatile uint32_t i;
    while (ms--)
    {
        i = 7000U;
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

uint8_t lcd_is_busy(void)
{
    return 0;
}

void lcd_hw_init(void)
{
    SPI_InitTypeDef spi;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

    LCD_SCL_IO_Init();
    LCD_SDA_IO_Init();
    LCD_RES_IO_Init();
    LCD_DC_IO_Init();
    LCD_CS_IO_Init();
    LCD_BL_IO_Init();
    lcd_bl_on();

    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
#if ((LCD_DEEP == DEEP_OLED) || (LCD_DEEP == DEEP_GRAY8))
    spi.SPI_CPOL = SPI_CPOL_High;
    spi.SPI_CPHA = SPI_CPHA_2Edge;
#else
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;
#endif
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 0;

    SPI_I2S_DeInit(LCD_SPIx);
    SPI_Init(LCD_SPIx, &spi);
    SPI_Cmd(LCD_SPIx, ENABLE);

    LCD_RES_Clr();
    lcd_delay_ms(100);
    LCD_RES_Set();
    lcd_delay_ms(100);

    lcd_ic_init();
}

void lcd_send_1Cmd(uint8_t dat)
{
    send_lcd_spi_done();
    LCD_DC_Clr();
    LCD_CS_Clr();
    send_lcd_spi_dat8(dat);
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
}

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
        send_lcd_spi_dat8(p[i++]);
    }
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
#else
    send_lcd_spi_done();
    spi_set_8b();
    LCD_DC_Clr();
		num--;
    LCD_CS_Clr();
    send_lcd_spi_dat8(*p++);
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_DC_Set();
    while (num-- > 0)
    {
        wait_lcd_spi_txtemp_free();
        send_lcd_spi_dat8(*p++);
    }
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
#endif
		
}

void lcd_send_1Dat(uint8_t dat)
{
    send_lcd_spi_done();
    LCD_DC_Set();
    LCD_CS_Clr();
    send_lcd_spi_dat8(dat);
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
}

void lcd_send_nDat(uint8_t *p, uint16_t num)
{
    uint16_t i = 0;
    send_lcd_spi_done();
    LCD_DC_Set();
    LCD_CS_Clr();
    while (num > i)
    {
        wait_lcd_spi_txtemp_free();
        send_lcd_spi_dat8(p[i++]);
    }
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
}

void lcd_oled_port(uint16_t x0, uint16_t x1, uint16_t page, uint8_t *page_gram)
{
    (void)x0;
    (void)x1;
    (void)page;
    (void)page_gram;
}

void lcd_gray_port(uint16_t x0, uint16_t x1, uint16_t page, uint8_t *page_gram)
{
    (void)x0;
    (void)x1;
    (void)page;
    (void)page_gram;
}

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
        send_lcd_spi_dat16(*gram++);
    }
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
    spi_set_8b();
}

void lcd_rgb888_port(uint8_t *gram, uint32_t pix_size)
{
    uint32_t pixel_count;
    uint8_t r, g, b;
    uint16_t rgb565;

    if (gram == NULL)
        return;

    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    spi_set_16b();
    LCD_DC_Set();
    LCD_CS_Clr();

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
        send_lcd_spi_dat16(rgb565);
    }

    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
    spi_set_8b();
}

#endif
