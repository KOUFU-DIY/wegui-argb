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

#if (LCD_IC == _SH1115)
#include "sh1115.h"

/* 设置一页的页地址 + 列起始地址（共享 flush 回调） */
void sh1115_set_addr(uint8_t page, uint8_t col)
{
    uint8_t cmds[3];
    col = (uint8_t)(col + SCREEN_X_OFFSET);
    page = (uint8_t)(page + (SCREEN_Y_OFFSET / 8));
    cmds[0] = (uint8_t)(0x10U | (col >> 4)); /* 列地址高 4 位 */
    cmds[1] = (uint8_t)(col & 0x0FU);        /* 列地址低 4 位 */
    cmds[2] = (uint8_t)(0xB0U + page);       /* 页地址 */
    lcd_send_nCmd(cmds, 3);
}

void sh1115_set_contrast(uint8_t value)
{
    lcd_send_1Cmd(0x81);
    lcd_send_1Cmd(value);
}

void sh1115_display_on(void)
{
    lcd_send_1Cmd(0xAF);
}

void sh1115_display_off(void)
{
    lcd_send_1Cmd(0xAE);
}

/* 清空 IC 内部 GDDRAM */
static void _sh1115_clear(void)
{
    uint8_t page, x;
    for (page = 0U; page < 8U; page++)
    {
        sh1115_set_addr(page, 0U);
        for (x = 0U; x < 128U; x++)
        {
            lcd_send_1Dat(0x00);
        }
    }
}

/*--------------------------------------------------------------
  * 名称: sh1115_init()
  * 功能: 初始化屏幕（128x64）
  * 说明: 推荐按屏厂资料微调
----------------------------------------------------------------*/
void sh1115_init(void)
{
    lcd_send_1Cmd(0xAE); /* 关显示 */
    lcd_send_1Cmd(0x00); /* 列地址低 4 位 */
    lcd_send_1Cmd(0x10); /* 列地址高 4 位 */
    lcd_send_1Cmd(0x40); /* 起始行 0 */
    lcd_send_1Cmd(0x81); /* 对比度 */
    lcd_send_1Cmd(0x88);
    lcd_send_1Cmd(0x82); /* IREF 参考电流 */
    lcd_send_1Cmd(0x00);
    lcd_send_1Cmd(0xA2); /* SEG 点阵排布顺序 0（默认） */
    lcd_send_1Cmd(0xA1); /* SEG 映射方向 1 */
    lcd_send_1Cmd(0xC8); /* COM 扫描方向 1 */
    lcd_send_1Cmd(0xA4); /* 关闭全亮测试 */
    lcd_send_1Cmd(0xA8); /* multiplex ratio */
    lcd_send_1Cmd(0x3F); /* 1/64 duty */
    lcd_send_1Cmd(0xD3); /* 显示偏移 */
    lcd_send_1Cmd(0x00);
    lcd_send_1Cmd(0xD5); /* 时钟（晶振偏移 0xA / 8 分频） */
    lcd_send_1Cmd(0xA0);
    lcd_send_1Cmd(0xD9); /* 预充电周期（放电 2 / 充电 2） */
    lcd_send_1Cmd(0x22);
    lcd_send_1Cmd(0xDB); /* VCOMH */
    lcd_send_1Cmd(0x40);
    lcd_send_1Cmd(0x31); /* 泵电压 1（30h~33h） */
    lcd_send_1Cmd(0xAD); /* DCDC */
    lcd_send_1Cmd(0x8B); /* 开（默认） */
    lcd_send_1Cmd(0xA6); /* 正常显示（非反色） */
    _sh1115_clear();
    lcd_delay_ms(200);
    lcd_send_1Cmd(0xAF); /* 开显示 */
}

#endif
