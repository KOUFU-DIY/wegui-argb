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

#if (LCD_IC == _SH1107)
#include "sh1107.h"

/* 设置一页的页地址 + 列起始地址（共享 flush 回调） */
void sh1107_set_addr(uint8_t page, uint8_t col)
{
    uint8_t cmds[3];
    col = (uint8_t)(col + SCREEN_X_OFFSET);
    page = (uint8_t)(page + (SCREEN_Y_OFFSET / 8));
    cmds[0] = (uint8_t)(0x10U | (col >> 4)); /* 列地址高 4 位 */
    cmds[1] = (uint8_t)(col & 0x0FU);        /* 列地址低 4 位 */
    cmds[2] = (uint8_t)(0xB0U + page);       /* 页地址 */
    lcd_send_nCmd(cmds, 3);
}

void sh1107_set_contrast(uint8_t value)
{
    lcd_send_1Cmd(0x81);
    lcd_send_1Cmd(value);
}

void sh1107_display_on(void)
{
    lcd_send_1Cmd(0xAF);
}

void sh1107_display_off(void)
{
    lcd_send_1Cmd(0xAE);
}

/* 清空 IC 内部 GDDRAM（SH1107 最大 128x128 = 16 页，全清） */
static void _sh1107_clear(void)
{
    uint8_t page, x;
    for (page = 0U; page < 16U; page++)
    {
        sh1107_set_addr(page, 0U);
        for (x = 0U; x < 128U; x++)
        {
            lcd_send_1Dat(0x00);
        }
    }
}

/*--------------------------------------------------------------
  * 名称: sh1107_init()
  * 功能: 初始化屏幕
  * 说明: 推荐按屏厂资料微调；方向由 0xC0/0xC8 与 0xA0/0xA1 组合控制
----------------------------------------------------------------*/
void sh1107_init(void)
{
    lcd_send_1Cmd(0xAE); /* 关显示 */
    lcd_send_1Cmd(0x00); /* 列地址低 4 位 */
    lcd_send_1Cmd(0x10); /* 列地址高 4 位 */
    lcd_send_1Cmd(0xB0); /* 页地址 */
    lcd_send_1Cmd(0xDC); /* 起始行 */
    lcd_send_1Cmd(0x00);
    lcd_send_1Cmd(0x81); /* 对比度 */
    lcd_send_1Cmd(0x6F);
    lcd_send_1Cmd(0x20); /* 页寻址模式 */
    lcd_send_1Cmd(0xA4); /* 关闭全亮测试 */
    lcd_send_1Cmd(0xA6); /* 正常显示（非反色） */
    lcd_send_1Cmd(0xA8); /* multiplex ratio */
    lcd_send_1Cmd(0x7F); /* 1/128 duty */
    lcd_send_1Cmd(0xD3); /* 显示偏移 */
    lcd_send_1Cmd(0x00);
    lcd_send_1Cmd(0xC0); /* COM 扫描方向（方向 1：C0+A0；方向 2：C8+A1） */
    lcd_send_1Cmd(0xA0); /* SEG 映射方向 */
    lcd_send_1Cmd(0xD5); /* 时钟分频 */
    lcd_send_1Cmd(0x80);
    lcd_send_1Cmd(0xD9); /* 预充电周期 */
    lcd_send_1Cmd(0x1D);
    lcd_send_1Cmd(0xDB); /* VCOMH */
    lcd_send_1Cmd(0x35);
    lcd_send_1Cmd(0xAD); /* 电荷泵 */
    lcd_send_1Cmd(0x80);
    _sh1107_clear();
    lcd_send_1Cmd(0xAF); /* 开显示 */
}

#endif
