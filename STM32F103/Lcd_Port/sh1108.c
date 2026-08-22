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

#if (LCD_IC == _SH1108)
#include "sh1108.h"

/* 设置一页的页地址 + 列起始地址（SH1108 页地址为 0xB0 + 独立数据字节，共 4 条命令） */
void sh1108_set_addr(uint8_t page, uint8_t col)
{
    uint8_t cmds[4];
    col = (uint8_t)(col + SCREEN_X_OFFSET);
    page = (uint8_t)(page + (SCREEN_Y_OFFSET / 8));
    cmds[0] = (uint8_t)(0x10U | (col >> 4)); /* 列地址高 4 位 */
    cmds[1] = (uint8_t)(col & 0x0FU);        /* 列地址低 4 位 */
    cmds[2] = 0xB0;                          /* 页地址命令 */
    cmds[3] = page;                          /* 页号 */
    lcd_send_nCmd(cmds, 4);
}

void sh1108_set_contrast(uint8_t value)
{
    lcd_send_1Cmd(0x81);
    lcd_send_1Cmd(value);
}

void sh1108_display_on(void)
{
    lcd_send_1Cmd(0xAF);
}

void sh1108_display_off(void)
{
    lcd_send_1Cmd(0xAE);
}

/* 清空 IC 内部 GDDRAM（160 列 x 20 页全清） */
static void _sh1108_clear(void)
{
    uint8_t page, x;
    for (page = 0U; page < 20U; page++)
    {
        sh1108_set_addr(page, 0U);
        for (x = 0U; x < 160U; x++)
        {
            lcd_send_1Dat(0x00);
        }
    }
}

/*--------------------------------------------------------------
  * 名称: sh1108_init()
  * 功能: 初始化屏幕（COM128 x SEG160）
  * 说明: 推荐按屏厂资料微调；DCDC/泵电压设置较复杂，详见规格书
----------------------------------------------------------------*/
void sh1108_init(void)
{
    lcd_send_1Cmd(0xAE); /* 关显示 */
    lcd_send_1Cmd(0x81); /* 对比度 */
    lcd_send_1Cmd(0xD0);
    lcd_send_1Cmd(0xA4); /* 关闭全亮测试 */
    lcd_send_1Cmd(0xA6); /* 正常显示（非反色） */
    lcd_send_1Cmd(0xA9); /* 驱动路数 */
    lcd_send_1Cmd(0x02); /* COM128_SEG160 */
    lcd_send_1Cmd(0xAD); /* DCDC */
    lcd_send_1Cmd(0x80);
    lcd_send_1Cmd(0xC0); /* COM 扫描方向（方向 1：C0+A0） */
    lcd_send_1Cmd(0xA0); /* SEG 映射方向 */
    lcd_send_1Cmd(0xD5); /* 时钟分频（晶振 15 / 分频 0） */
    lcd_send_1Cmd(0xF0);
    lcd_send_1Cmd(0xD9); /* 预充电周期（放电 2 / 充电 15） */
    lcd_send_1Cmd(0x2F);
    lcd_send_1Cmd(0xDB); /* VCOMH */
    lcd_send_1Cmd(0x3F);
    lcd_send_1Cmd(0x20); /* 页寻址模式 */
    lcd_send_1Cmd(0xDC); /* VSEGM */
    lcd_send_1Cmd(0x35);
    lcd_send_1Cmd(0x30); /* VSL 0 */
    _sh1108_clear();
    lcd_delay_ms(200);
    lcd_send_1Cmd(0xAF); /* 开显示 */
}

#endif
