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

#if (LCD_PORT == _SOFT_4SPI)
#include "stm32f030_lcd_soft_4spi_port.h"
#include "we_gui_driver.h"

/* 软件延时，无需精准 */
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
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | RCC_AHBPeriph_GPIOB, ENABLE);

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

    lcd_ic_init();
}

/*--------------------------------------------------------------
  * 名称: lcd_spi_send_1byte(uint8_t dat)
  * 传入: dat
  * 功能: SPI发送1个字节数据
----------------------------------------------------------------*/
static void lcd_spi_send_1byte(uint8_t dat)
{
#if ((LCD_DEEP == DEEP_OLED) || (LCD_DEEP == DEEP_GRAY8))
    // OLED SPI SCL默认常高,第二个跳变数据沿有效
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        LCD_SCL_Clr();
        if (dat & 0x80)
            LCD_SDA_Set();
        else
            LCD_SDA_Clr();
        dat <<= 1;
        LCD_SCL_Set();
    }
#else
    // TFT SPI SCL默认常低,第一个跳变数据沿有效
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        if (dat & 0x80)
            LCD_SDA_Set();
        else
            LCD_SDA_Clr();
        LCD_SCL_Set();
        dat <<= 1;
        LCD_SCL_Clr();
    }
#endif
}

/* 发送 1 个命令字节 (DC=0) */
void lcd_send_1Cmd(uint8_t dat)
{
    LCD_DC_Clr();
    LCD_CS_Clr();
    lcd_spi_send_1byte(dat);
    LCD_CS_Set();
}

/* 发送命令帧：p[0] 为命令，p[1..] 为跟随数据 */
void lcd_send_nCmd(uint8_t *p, uint16_t num)
{
	//-----------------OLED类型屏幕-------------------
#if ((LCD_DEEP == DEEP_OLED) || (LCD_DEEP == DEEP_GRAY8))
		uint16_t i=0;
		LCD_DC_Clr();
		LCD_CS_Clr();
		while(num>i)
		{
			lcd_spi_send_1byte(p[i++]);	  
		}
		LCD_CS_Set();
#else
	//-----------------TFT类型屏幕-------------------
		//数组[0]按照指令发送
		//余下位按照数据发送
		
		//1.开始传输第一个命令
		LCD_DC_Clr();
		num--;
		LCD_CS_Clr();
		lcd_spi_send_1byte(p[0]);
	
		//2.发送剩下的按数据发送
		LCD_DC_Set();
		while(num-->0)
		{
			p++;
			lcd_spi_send_1byte(*p);
		}
		
		//3.结束
		LCD_CS_Set();
	#endif
}

/* 发送 1 个数据字节 (DC=1) */
void lcd_send_1Dat(uint8_t dat)
{
    LCD_DC_Set();
    LCD_CS_Clr();
    lcd_spi_send_1byte(dat);
    LCD_CS_Set();
}

/* 发送 num 个数据字节 (轮询) */
void lcd_send_nDat(uint8_t *p, uint16_t num)
{
    uint16_t i = 0;

    if ((p == NULL) || (num == 0))
        return;

    LCD_DC_Set();
    LCD_CS_Clr();
    while (num > i)
    {
        lcd_spi_send_1byte(p[i++]);
    }
    LCD_CS_Set();
}

void lcd_rgb565_port(uint16_t *gram, uint32_t pix_size)
{
    uint32_t i;
    uint16_t color;

    LCD_DC_Set();
    LCD_CS_Clr();
    for (i = 0; i < pix_size; i++)
    {
        color = gram[i];
        lcd_spi_send_1byte((uint8_t)(color >> 8));
        lcd_spi_send_1byte((uint8_t)(color & 0xFFU));
    }
    LCD_CS_Set();
}

void lcd_rgb888_port(uint8_t *gram, uint32_t pix_size)
{
    if (gram == NULL)
        return;

    LCD_DC_Set();
    LCD_CS_Clr();
    while (pix_size--)
    {
        lcd_spi_send_1byte(*gram++);
    }
    LCD_CS_Set();
}

#endif
