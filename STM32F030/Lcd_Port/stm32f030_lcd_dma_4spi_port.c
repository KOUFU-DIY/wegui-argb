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

#include "stm32f030_hw_config.h"

#if (LCD_PORT == _DMA_4SPI)
#include "stm32f030_lcd_dma_4spi_port.h"
#include "we_gui_driver.h"

static volatile uint8_t s_busy;

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
    return s_busy;
}

void lcd_hw_init(void)
{
    SPI_InitTypeDef spi;
    DMA_InitTypeDef dma;
    NVIC_InitTypeDef nvic;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPIx, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

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

    DMA_DeInit(LCD_DMA_CHANNELx);
    dma.DMA_PeripheralBaseAddr = (uint32_t)LCD_DMA_PeripheralBaseAddr;
    dma.DMA_MemoryBaseAddr = (uint32_t)0x00;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = 1;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_VeryHigh;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(LCD_DMA_CHANNELx, &dma);

    DMA_RemapConfig(LCD_DMAx, LCD_DMA_REMAP_REQ);
    DMA_ClearFlag(LCD_DMA_COMPLETE_FLAG);
    DMA_ClearITPendingBit(LCD_DMA_COMPLETE_IT);
    DMA_ITConfig(LCD_DMA_CHANNELx, DMA_IT_TC, ENABLE);
    SPI_I2S_DMACmd(LCD_SPIx, SPI_I2S_DMAReq_Tx, ENABLE);

    nvic.NVIC_IRQChannel = DMA1_Channel4_5_IRQn;
    nvic.NVIC_IRQChannelPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    LCD_RES_Clr();
    lcd_delay_ms(100);
    LCD_RES_Set();
    lcd_delay_ms(100);

    s_busy = 0;
    lcd_ic_init();
}

void lcd_send_1Cmd(uint8_t dat)
{
    wait_lcd_dma_free();
    send_lcd_spi_done();
    spi_set_8b();
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
    wait_lcd_dma_free();
    send_lcd_spi_done();
    spi_set_8b();
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
    wait_lcd_dma_free();
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
    wait_lcd_dma_free();
    send_lcd_spi_done();
    spi_set_8b();
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

    wait_lcd_dma_free();
    send_lcd_spi_done();
    spi_set_8b();
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

static void dma_send_16b(uint16_t *p, uint32_t num)
{
    wait_lcd_dma_free();
    send_lcd_spi_done();
    spi_set_16b();
    LCD_DC_Set();
    LCD_CS_Clr();

    DMA_Cmd(LCD_DMA_CHANNELx, DISABLE);
    DMA_ClearFlag(LCD_DMA_COMPLETE_FLAG);
    DMA_ClearITPendingBit(LCD_DMA_COMPLETE_IT);
    LCD_DMA_CHANNELx->CMAR = (uint32_t)p;
    LCD_DMA_CHANNELx->CNDTR = num;
    s_busy = 1;
    DMA_Cmd(LCD_DMA_CHANNELx, ENABLE);
}

void lcd_rgb565_port(uint16_t *gram, uint32_t pix_size)
{
    if ((gram == NULL) || (pix_size == 0U))
        return;

    dma_send_16b(gram, pix_size);

#if (GRAM_DMA_BUFF_EN == 0)
    wait_lcd_dma_free();
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
#endif
}

void lcd_rgb888_port(uint8_t *gram, uint32_t pix_size)
{
    uint32_t pixel_count;
    uint8_t r, g, b;
    uint16_t rgb565;

    if (gram == NULL)
        return;

    wait_lcd_dma_free();
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

void lcd_dma_irq_handler(void)
{
    if (DMA_GetITStatus(LCD_DMA_COMPLETE_IT) != RESET)
    {
        DMA_Cmd(LCD_DMA_CHANNELx, DISABLE);
        DMA_ClearITPendingBit(LCD_DMA_COMPLETE_IT);
        DMA_ClearFlag(LCD_DMA_COMPLETE_FLAG);
        wait_lcd_spi_txtemp_free();
        send_lcd_spi_done();
        s_busy = 0;
#if (GRAM_DMA_BUFF_EN)
        LCD_CS_Set();
#endif
    }
}

#endif
