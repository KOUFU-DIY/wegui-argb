/*
	Copyright 2025 Lu Zhihao

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "we_hw_port.h"

#if (LCD_IC == _SSD1327)
#include "ssd1327.h"

/* 0xA0 重映射参数（1.5 寸 128x128 模组实测定案 0x51）：
 *   bit0 列地址重映射（字节列左右镜像）   bit1 nibble 重映射（字节内两像素互换）
 *   bit2 垂直地址递增——本驱动行主序刷屏，必须为 0（屏厂例程按列写屏
 *        所以带 bit2=1，其 0xA0 参数不可照抄）
 *   bit4 COM 重映射（上下翻转）           bit6 COM 奇偶分割（错了呈横向梳齿交错）
 * 换安装方向：上下翻转动 bit4，左右镜像动 bit0+bit1（两位一起）。 */
#define SSD1327_REMAP (0x51)

/*--------------------------------------------------------------
  * 名称: ssd1327_set_addr(x0, y0, x1, y1)
  * 功能: 设置矩形刷新窗口（列地址 2 像素/单位；ALIGN_X=2 保证对齐；
  *       窗口内 IC 自动递增，跨 flush 切片连续，刷屏由端口层
  *       lcd_gray_port 完成）
----------------------------------------------------------------*/
void ssd1327_set_addr(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t cmds[6];

    x0 = (uint16_t)(x0 + SCREEN_X_OFFSET);
    x1 = (uint16_t)(x1 + SCREEN_X_OFFSET);
    y0 = (uint16_t)(y0 + SCREEN_Y_OFFSET);
    y1 = (uint16_t)(y1 + SCREEN_Y_OFFSET);

    cmds[0] = 0x15; /* 列地址（字节列 = 2 像素） */
    cmds[1] = (uint8_t)(x0 >> 1);
    cmds[2] = (uint8_t)(x1 >> 1);
    cmds[3] = 0x75; /* 行地址 */
    cmds[4] = (uint8_t)y0;
    cmds[5] = (uint8_t)y1;
    lcd_send_nCmd(cmds, 6);
}

void ssd1327_set_contrast(uint8_t value)
{
    lcd_send_1Cmd(0x81);
    lcd_send_1Cmd(value);
}

void ssd1327_display_on(void)
{
    lcd_send_1Cmd(0xAF);
}

void ssd1327_display_off(void)
{
    lcd_send_1Cmd(0xAE);
}

/* 清空 IC 内部 GDDRAM（128x128 / 2 字节） */
static void _ssd1327_clear(void)
{
    uint32_t i;
    ssd1327_set_addr(0, 0, 127, 127);
    for (i = 0; i < (128UL * 128UL / 2UL); i++)
    {
        lcd_send_1Dat(0x00);
    }
}

/*--------------------------------------------------------------
  * 名称: ssd1327_init()
  * 功能: 初始化屏幕（128x128 灰度）
  * 说明: 0xA0 重映射字节控制扫描方向，需与屏厂资料匹配
----------------------------------------------------------------*/
void ssd1327_init(void)
{
    lcd_send_1Cmd(0xAE); /* 关显示 */

    /* 重映射：参数定义与试屏说明见文件顶部 SSD1327_REMAP */
    lcd_send_1Cmd(0xA0);
    lcd_send_1Cmd(SSD1327_REMAP);

    lcd_send_1Cmd(0xA1); /* 起始行 */
    lcd_send_1Cmd(0x00);
    lcd_send_1Cmd(0xA2); /* 显示偏移 */
    lcd_send_1Cmd(0x00);
    lcd_send_1Cmd(0xA4); /* 正常显示 */
    lcd_send_1Cmd(0xA8); /* multiplex ratio */
    lcd_send_1Cmd(0x7F); /* 1/128 duty */
    lcd_send_1Cmd(0xAB); /* Function Selection A */
    lcd_send_1Cmd(0x01); /* 内部 VDD 稳压器使能 */
    lcd_send_1Cmd(0x81); /* 对比度 */
    lcd_send_1Cmd(0x77);
    lcd_send_1Cmd(0xB1); /* Phase Length */
    lcd_send_1Cmd(0x31);
    lcd_send_1Cmd(0xB3); /* 时钟分频/晶振频率 */
    lcd_send_1Cmd(0xB1);
    lcd_send_1Cmd(0xB5); /* GPIO */
    lcd_send_1Cmd(0x03);
    lcd_send_1Cmd(0xB6); /* Second pre-charge Period */
    lcd_send_1Cmd(0x0D);
    lcd_send_1Cmd(0xBC); /* Pre-charge voltage */
    lcd_send_1Cmd(0x07);
    lcd_send_1Cmd(0xBE); /* VCOMH */
    lcd_send_1Cmd(0x07);
    lcd_send_1Cmd(0xD5); /* Function Selection B */
    lcd_send_1Cmd(0x02); /* 使能 second pre-charge */
    _ssd1327_clear();
    lcd_send_1Cmd(0xAF); /* 开显示 */
}

#endif
