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
#include "we_port.h"

#if (LCD_IC == _ST7789V3)
#include "st7789v3.h"

void st7789v3_set_addr(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	x1 += SCREEN_X_OFFSET;
	x2 += SCREEN_X_OFFSET;
	y1 += SCREEN_Y_OFFSET;
	y2 += SCREEN_Y_OFFSET;
	uint8_t i[]={0x2a,x1>>8,x1&0xff,x2>>8,x2&0xff};
	uint8_t j[]={0x2b,y1>>8,y1&0xff,y2>>8,y2&0xff};
	const uint8_t k[]={0x2c};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
	lcd_send_nCmd((uint8_t*)j,sizeof(j)/sizeof(uint8_t));
	lcd_send_nCmd((uint8_t*)k,sizeof(k)/sizeof(uint8_t));
}

void ST7789V3_Clear()//清除IC显示缓存
{
	uint32_t i;
	st7789v3_set_addr(0,0,320-1,320-1);
	i=0;
	while(i++<(uint32_t)320*320)
	{
		lcd_send_1Dat(0x33);lcd_send_1Dat(0x33);//测试
		//lcd_send_1Dat(0x00);lcd_send_1Dat(0x00);
	}
}
/*--------------------------------------------------------------
  * 名称: st7789v3_init()
  * 传入: 无
  * 返回: 无
  * 功能: 初始化屏幕
  * 说明: 推荐更改为屏幕资料中的初始化指令
----------------------------------------------------------------*/
/*
void st7789v3_init()
{

//	LCD_RES_Clr();
//	lcd_delay_ms(100);
//	LCD_RES_Set();
//	lcd_delay_ms(100);

	lcd_send_1Cmd(0x11); 
//	delay_1ms(120); 
	lcd_send_1Cmd(0x36); 
	lcd_send_1Dat(0x00);//方向1
	//lcd_send_1Dat(0xC0);//方向2
	//lcd_send_1Dat(0x70);//方向3
	//lcd_send_1Dat(0xA0);//方向4

	lcd_send_1Cmd(0x3A);
	lcd_send_1Dat(0x05);

	lcd_send_1Cmd(0xB2);
	lcd_send_1Dat(0x0C);
	lcd_send_1Dat(0x0C);
	lcd_send_1Dat(0x00);
	lcd_send_1Dat(0x33);
	lcd_send_1Dat(0x33); 

	lcd_send_1Cmd(0xB7); 
	lcd_send_1Dat(0x35);  

	lcd_send_1Cmd(0xBB);
	lcd_send_1Dat(0x35);

	lcd_send_1Cmd(0xC0);
	lcd_send_1Dat(0x2C);

	lcd_send_1Cmd(0xC2);
	lcd_send_1Dat(0x01);

	lcd_send_1Cmd(0xC3);
	lcd_send_1Dat(0x13);   

	lcd_send_1Cmd(0xC4);
	lcd_send_1Dat(0x20);  

	lcd_send_1Cmd(0xC6); 
	lcd_send_1Dat(0x0F);    

	lcd_send_1Cmd(0xD0); 
	lcd_send_1Dat(0xA4);
	lcd_send_1Dat(0xA1);

	lcd_send_1Cmd(0xD6); 
	lcd_send_1Dat(0xA1);

	lcd_send_1Cmd(0xE0);
	lcd_send_1Dat(0xF0);
	lcd_send_1Dat(0x00);
	lcd_send_1Dat(0x04);
	lcd_send_1Dat(0x04);
	lcd_send_1Dat(0x04);
	lcd_send_1Dat(0x05);
	lcd_send_1Dat(0x29);
	lcd_send_1Dat(0x33);
	lcd_send_1Dat(0x3E);
	lcd_send_1Dat(0x38);
	lcd_send_1Dat(0x12);
	lcd_send_1Dat(0x12);
	lcd_send_1Dat(0x28);
	lcd_send_1Dat(0x30);

	lcd_send_1Cmd(0xE1);
	lcd_send_1Dat(0xF0);
	lcd_send_1Dat(0x07);
	lcd_send_1Dat(0x0A);
	lcd_send_1Dat(0x0D);
	lcd_send_1Dat(0x0B);
	lcd_send_1Dat(0x07);
	lcd_send_1Dat(0x28);
	lcd_send_1Dat(0x33);
	lcd_send_1Dat(0x3E);
	lcd_send_1Dat(0x36);
	lcd_send_1Dat(0x14);
	lcd_send_1Dat(0x14);
	lcd_send_1Dat(0x29);
	lcd_send_1Dat(0x32);
	
// 	lcd_send_1Cmd(0x2A);
//	lcd_send_1Dat(0x00);
//	lcd_send_1Dat(0x22);
//	lcd_send_1Dat(0x00);
//	lcd_send_1Dat(0xCD);
//	lcd_send_1Dat(0x2B);
//	lcd_send_1Dat(0x00);
//	lcd_send_1Dat(0x00);
//	lcd_send_1Dat(0x01);
//	lcd_send_1Dat(0x3F);
//	lcd_send_1Cmd(0x2C);
	lcd_send_1Cmd(0x21); 
  
  lcd_send_1Cmd(0x11);
  lcd_delay_ms(120);	
	lcd_send_1Cmd(0x29); 
	ST7789V3_Clear();//清除IC显示缓存
}
*/
void st7789v3_init()
{
	LCD_RES_Set();
	lcd_delay_ms(200);
	LCD_RES_Clr();
	lcd_delay_ms(200);
	LCD_RES_Set();
	lcd_delay_ms(200);
	
	lcd_send_1Cmd(0x11); 
	lcd_delay_ms(200);
	
	#define LCD_1IN69_SendCommand   lcd_send_1Cmd
	#define LCD_1IN69_SendData_8Bit lcd_send_1Dat
	#define DEV_Delay_ms            lcd_delay_ms
	
		LCD_1IN69_SendCommand(0x36);
    LCD_1IN69_SendData_8Bit(0x60);

	#if LCD_DEEP == DEEP_RGB565
    LCD_1IN69_SendCommand(0x3A);
    LCD_1IN69_SendData_8Bit(0x05);
	#elif LCD_DEEP == DEEP_RGB888
		LCD_1IN69_SendCommand(0x3A);
    LCD_1IN69_SendData_8Bit(0x07);
	#endif

    LCD_1IN69_SendCommand(0xB2);
    LCD_1IN69_SendData_8Bit(0x0B);
    LCD_1IN69_SendData_8Bit(0x0B);
    LCD_1IN69_SendData_8Bit(0x00);
    LCD_1IN69_SendData_8Bit(0x33);
    LCD_1IN69_SendData_8Bit(0x35);

    LCD_1IN69_SendCommand(0xB7);
    LCD_1IN69_SendData_8Bit(0x11);

    LCD_1IN69_SendCommand(0xBB);
    LCD_1IN69_SendData_8Bit(0x35);

    LCD_1IN69_SendCommand(0xC0);
    LCD_1IN69_SendData_8Bit(0x2C);

    LCD_1IN69_SendCommand(0xC2);
    LCD_1IN69_SendData_8Bit(0x01);

    LCD_1IN69_SendCommand(0xC3);
    LCD_1IN69_SendData_8Bit(0x0D);

    LCD_1IN69_SendCommand(0xC4);
    LCD_1IN69_SendData_8Bit(0x20);

    LCD_1IN69_SendCommand(0xC6);
    LCD_1IN69_SendData_8Bit(0x13);

    LCD_1IN69_SendCommand(0xD0);
    LCD_1IN69_SendData_8Bit(0xA4);
    LCD_1IN69_SendData_8Bit(0xA1);

    LCD_1IN69_SendCommand(0xD6);
    LCD_1IN69_SendData_8Bit(0xA1);

    LCD_1IN69_SendCommand(0xE0);
    LCD_1IN69_SendData_8Bit(0xF0);
    LCD_1IN69_SendData_8Bit(0x06);
    LCD_1IN69_SendData_8Bit(0x0B);
    LCD_1IN69_SendData_8Bit(0x0A);
    LCD_1IN69_SendData_8Bit(0x09);
    LCD_1IN69_SendData_8Bit(0x26);
    LCD_1IN69_SendData_8Bit(0x29);
    LCD_1IN69_SendData_8Bit(0x33);
    LCD_1IN69_SendData_8Bit(0x41);
    LCD_1IN69_SendData_8Bit(0x18);
    LCD_1IN69_SendData_8Bit(0x16);
    LCD_1IN69_SendData_8Bit(0x15);
    LCD_1IN69_SendData_8Bit(0x29);
    LCD_1IN69_SendData_8Bit(0x2D);

    LCD_1IN69_SendCommand(0xE1);
    LCD_1IN69_SendData_8Bit(0xF0);
    LCD_1IN69_SendData_8Bit(0x04);
    LCD_1IN69_SendData_8Bit(0x08);
    LCD_1IN69_SendData_8Bit(0x08);
    LCD_1IN69_SendData_8Bit(0x07);
    LCD_1IN69_SendData_8Bit(0x03);
    LCD_1IN69_SendData_8Bit(0x28);
    LCD_1IN69_SendData_8Bit(0x32);
    LCD_1IN69_SendData_8Bit(0x40);
    LCD_1IN69_SendData_8Bit(0x3B);
    LCD_1IN69_SendData_8Bit(0x19);
    LCD_1IN69_SendData_8Bit(0x18);
    LCD_1IN69_SendData_8Bit(0x2A);
    LCD_1IN69_SendData_8Bit(0x2E);

    LCD_1IN69_SendCommand(0xE4);
    LCD_1IN69_SendData_8Bit(0x25);
    LCD_1IN69_SendData_8Bit(0x00);
    LCD_1IN69_SendData_8Bit(0x00);

    LCD_1IN69_SendCommand(0x21);

    LCD_1IN69_SendCommand(0x11);
    DEV_Delay_ms(120);
    LCD_1IN69_SendCommand(0x29);
 
}
#endif
