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

#if (LCD_PORT == _DMA_4SPI)
#include "stm32f103_lcd_dma_4spi_port.h"
#include "we_gui_driver.h"

/* DMA 忙碌标志，lcd_is_busy() 对外暴露，中断中清零 */
static volatile uint8_t s_busy;

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
    volatile uint16_t i;
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

/*
 * lcd_is_busy() 保持普通外部函数，不内联，
 * 保证所有优化等级下都能稳定生成符号（wait_lcd_dma_free 宏依赖它）。
 */
uint8_t lcd_is_busy(void)
{
    return s_busy;
}

void lcd_hw_init(void)
{
    SPI_InitTypeDef  spi;
    DMA_InitTypeDef  dma;
    NVIC_InitTypeDef nvic;

    spi.SPI_Direction = SPI_Direction_1Line_Tx;
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

    spi.SPI_NSS               = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = LCD_SPI_BaudRatePrescaler_x;
    spi.SPI_FirstBit          = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial     = 7;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPIx, ENABLE);
    RCC_PCLK2Config(RCC_HCLK_Divx);
    SPI_Init(LCD_SPIx, &spi);

    /* DMA 初始化 */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    dma.DMA_PeripheralBaseAddr = (uint32_t)LCD_DMA_PeripheralBaseAddr;
    dma.DMA_MemoryBaseAddr     = (uint32_t)0x00;
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_VeryHigh;
    dma.DMA_M2M                = DMA_M2M_Disable;
    dma.DMA_BufferSize         = 1;
    DMA_DeInit(LCD_DMA_CHANNELx);
    DMA_Init(LCD_DMA_CHANNELx, &dma);

    SPI_I2S_DMACmd(LCD_SPIx, SPI_I2S_DMAReq_Tx, ENABLE);
    SPI_Cmd(LCD_SPIx, ENABLE);
    DMA_ITConfig(LCD_DMA_CHANNELx, DMA_IT_TC, ENABLE);

    /* DMA 传输完成中断 */
    nvic.NVIC_IRQChannel                   = DMA1_Channel3_IRQn;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    NVIC_Init(&nvic);

    LCD_SCL_IO_Init();
    LCD_SDA_IO_Init();
    LCD_RES_IO_Init();
    LCD_DC_IO_Init();
    LCD_CS_IO_Init();
    LCD_BL_IO_Init();

    LCD_RES_Clr();
    lcd_delay_ms(100);
    LCD_RES_Set();
    lcd_delay_ms(100);

    s_busy = 0;
    lcd_ic_init();
}

/* 发送 1 个命令字节 (DC=0) */
void lcd_send_1Cmd(uint8_t dat)
{
    wait_lcd_dma_free();
    send_lcd_spi_done();
    spi_set_8b();
    LCD_DC_Clr();
    LCD_CS_Clr();
    send_lcd_spi_dat(dat);
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
}

/* 发送 1 个数据字节 (DC=1) */
void lcd_send_1Dat(uint8_t dat)
{
    wait_lcd_dma_free();
    wait_lcd_spi_txtemp_free();
    spi_set_8b();
    LCD_DC_Set();
    LCD_CS_Clr();
    send_lcd_spi_dat(dat);
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
}

/* 发送命令帧：p[0] 为命令，p[1..] 为跟随数据 */
void lcd_send_nCmd(uint8_t *p, uint16_t num)
{
    wait_lcd_dma_free();
    send_lcd_spi_done();

#if ((LCD_DEEP == DEEP_OLED) || (LCD_DEEP == DEEP_GRAY8))
    uint16_t i = 0;
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

/* 发送 num 个数据字节 (轮询) */
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
        send_lcd_spi_dat(p[i++]);
    }
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
}

/* 内部：DMA 发送 16 位半字数组 (异步) */
static void dma_send_16b(uint16_t *p, uint32_t num)
{
    wait_lcd_dma_free();
    send_lcd_spi_done();
    spi_set_16b();
    LCD_DC_Set();
    LCD_CS_Clr();

    s_busy = 1;
    LCD_DMA_CHANNELx->CMAR  = (uint32_t)p;
    LCD_DMA_CHANNELx->CNDTR = (uint32_t)num;
    DMA_Cmd(LCD_DMA_CHANNELx, ENABLE);
}

/* RGB565 刷屏: DMA 16 位异步推流 */
void lcd_rgb565_port(uint16_t *gram, uint32_t pix_size)
{
    dma_send_16b(gram, pix_size);

#if (GRAM_DMA_BUFF_EN == 0)
    wait_lcd_dma_free();
    wait_lcd_spi_txtemp_free();
    send_lcd_spi_done();
    LCD_CS_Set();
#endif
}

/* RGB888 刷屏: DMA 8 位推流 (原始字节流直发) */
void lcd_rgb888_port(uint8_t *gram, uint32_t pix_size)
{
    if (gram == NULL)
    {
        return;
    }

    wait_lcd_dma_free();
    wait_lcd_spi_txtemp_free();
    spi_set_8b();
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
}

/* DMA 传输完成中断 */
void DMA1_Channel3_IRQHandler(void)
{
    DMA_Cmd(LCD_DMA_CHANNELx, DISABLE);
    DMA_ClearFlag(LCD_DMA_COMPLETE_FLAG);
    wait_lcd_spi_txtemp_free(); /* DMA 完毕不代表 SPI 发完 */
    send_lcd_spi_done();
    s_busy = 0;

#if (GRAM_DMA_BUFF_EN)
    LCD_CS_Set();
#endif
}

#endif
