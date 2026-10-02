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

#if (LCD_PORT == _SOFT_IIC)
#include "stm32f103_lcd_soft_iic_port.h"
#include "wegui.h"

#define _IIC_ADDR_W  ((uint8_t)(OLED_IIC_7ADDR << 1)) /* 写方向从机地址 */
#define _IIC_CTRL_CMD (0x00U)                          /* 控制字节：后续全为命令 */
#define _IIC_CTRL_DAT (0x40U)                          /* 控制字节：后续全为数据 */

/* ---- 位翻转 I2C 原语 ---- */

static void _iic_delay(void)
{
#if (LCD_IIC_DELAY_NOPS > 0)
    volatile uint8_t n = LCD_IIC_DELAY_NOPS;
    while (n--)
        __NOP();
#endif
}

/* START：SCL 高电平期间 SDA 下拉 */
static void _iic_start(void)
{
    LCD_SDA_Set();
    LCD_SCL_Set();
    _iic_delay();
    LCD_SDA_Clr();
    _iic_delay();
    LCD_SCL_Clr();
}

/* STOP：SCL 高电平期间 SDA 释放回高 */
static void _iic_stop(void)
{
    LCD_SDA_Clr();
    _iic_delay();
    LCD_SCL_Set();
    _iic_delay();
    LCD_SDA_Set();
    _iic_delay();
}

/* 发送 1 字节（MSB 先行）并完成第 9 个 ACK 时钟；ACK 电平不作为错误处理，
 * 但必须读走以维持时序（从机拉低 SDA 应答） */
static void _iic_send_byte(uint8_t b)
{
    uint8_t i;

    for (i = 0U; i < 8U; i++)
    {
        if ((b & 0x80U) != 0U)
            LCD_SDA_Set();
        else
            LCD_SDA_Clr();
        b = (uint8_t)(b << 1);
        _iic_delay();
        LCD_SCL_Set();
        _iic_delay();
        LCD_SCL_Clr();
    }

    LCD_SDA_Set(); /* 释放 SDA，从机在第 9 拍应答 */
    _iic_delay();
    LCD_SCL_Set();
    _iic_delay();
    (void)LCD_SDA_Read();
    LCD_SCL_Clr();
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

/* OLED 自发光，无背光引脚 */
void lcd_bl_on(void)
{
}

void lcd_bl_off(void)
{
}

/* 阻塞式发送，接口始终空闲 */
uint8_t lcd_is_busy(void)
{
    return 0;
}

void lcd_hw_init(void)
{
    LCD_SCL_IO_Init();
    LCD_SDA_IO_Init();
    LCD_RES_IO_Init();

    LCD_RES_Clr();
    lcd_delay_ms(100);
    LCD_RES_Set();
    lcd_delay_ms(100);

    lcd_ic_init();
}

/* 发送 1 个命令字节 */
void lcd_send_1Cmd(uint8_t dat)
{
    _iic_start();
    _iic_send_byte(_IIC_ADDR_W);
    _iic_send_byte(_IIC_CTRL_CMD);
    _iic_send_byte(dat);
    _iic_stop();
}

/* 发送命令帧：OLED/灰度 IC 的初始化序列整帧全为命令字节 */
void lcd_send_nCmd(uint8_t *p, uint16_t num)
{
    uint16_t i;

    _iic_start();
    _iic_send_byte(_IIC_ADDR_W);
    _iic_send_byte(_IIC_CTRL_CMD);
    for (i = 0U; i < num; i++)
        _iic_send_byte(p[i]);
    _iic_stop();
}

/* 发送 1 个数据字节 */
void lcd_send_1Dat(uint8_t dat)
{
    _iic_start();
    _iic_send_byte(_IIC_ADDR_W);
    _iic_send_byte(_IIC_CTRL_DAT);
    _iic_send_byte(dat);
    _iic_stop();
}

/* 发送 num 个数据字节 */
void lcd_send_nDat(uint8_t *p, uint16_t num)
{
    uint16_t i;

    _iic_start();
    _iic_send_byte(_IIC_ADDR_W);
    _iic_send_byte(_IIC_CTRL_DAT);
    for (i = 0U; i < num; i++)
        _iic_send_byte(p[i]);
    _iic_stop();
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
 * 垂直 8 像素打包成页字节，每页一个 I2C 数据帧边转换边发送。
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

        _iic_start();
        _iic_send_byte(_IIC_ADDR_W);
        _iic_send_byte(_IIC_CTRL_DAT);
        for (x = 0; x < width; x++)
        {
            uint8_t byte = 0U;
            uint8_t bit;

            for (bit = 0U; bit < 8U; bit++)
            {
                if (base[(uint32_t)bit * width + x] >= WE_OLED_GRAY_THRESHOLD)
                    byte |= (uint8_t)(1U << bit);
            }
            _iic_send_byte(byte);
        }
        _iic_stop();
    }

    lcd_win_cur_y = (uint16_t)(lcd_win_cur_y + rows);
}

#endif /* LCD_TYPE == HW_LCD_OLED */

#if (LCD_TYPE == HW_LCD_GRAY)

/* ---- 灰度 OLED（SSD1327 类）刷屏：GRAY8 -> 4bpp 双像素打包 ----
 * 窗口寻址由 lcd_set_windows 转发，IC 窗口内自动递增，跨 flush 切片连续；
 * 每字节 2 像素：高 4 位 = 左像素，ALIGN_X=2 保证偶数宽。 */
void lcd_gray_port(uint8_t *gram, uint32_t pix_size)
{
    uint16_t width = (uint16_t)(lcd_win_x1 - lcd_win_x0 + 1U);
    uint16_t rows, row, pairs, x;

    if ((gram == 0) || (width == 0U))
        return;

    rows  = (uint16_t)(pix_size / width);
    pairs = (uint16_t)(width >> 1);

    _iic_start();
    _iic_send_byte(_IIC_ADDR_W);
    _iic_send_byte(_IIC_CTRL_DAT);
    for (row = 0; row < rows; row++)
    {
        uint8_t *src = gram + ((uint32_t)row * width);

        for (x = 0; x < pairs; x++)
            _iic_send_byte((uint8_t)((src[2U * x] & 0xF0U) | (src[2U * x + 1U] >> 4)));
    }
    _iic_stop();
}

#endif /* LCD_TYPE == HW_LCD_GRAY */

#endif /* LCD_PORT == _SOFT_IIC */
