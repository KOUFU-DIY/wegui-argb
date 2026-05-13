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

#if (LCD_IC == _ST7735)
#include "st7735.h"

void st7735_set_addr(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
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

void st7735_Soft_Reset()
{
	const uint8_t i[]={0x01};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_sleep_in()
{
	const uint8_t i[]={0x10};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_sleep_out()
{
	const uint8_t i[]={0x11};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_Partial_Mode_On()
{
	const uint8_t i[]={0x12};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_Partial_Mode_Off()
{
	const uint8_t i[]={0x13};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_Inversion_Off()
{
	const uint8_t i[]={0x20};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_Inversion_On()
{
	const uint8_t i[]={0x21};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_Display_Off()
{
	const uint8_t i[]={0x28};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_Display_On()
{
	const uint8_t i[]={0x29};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_TE_Line_Off()
{
	const uint8_t i[]={0x34};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_TE_Line_On()
{
	#define TELOM 0//[0:1]When TELOM =’0’: The Tearing Efonly
	const uint8_t i[]={0x35,TELOM};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_Idle_Off()
{
	const uint8_t i[]={0x38};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_Idle_On()
{
	const uint8_t i[]={0x39};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_Set_RGB565_Mode()
{
	uint8_t i[]={0x3A,0x05};//65k(RGB565)
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_Set_RGB444_Mode()
{
	uint8_t i[]={0x3A,0x03};//4k(RGB444)
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void st7735_Set_RGB666_Mode()
{
	uint8_t i[]={0x3A,0x06};//262k(RGB666)
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}

void st7735_Clear()//清除IC显示缓存
{
	uint16_t i=0;
	st7735_set_addr(0,0,162-1,162-1);
	while(i++<162*162)
	{
		lcd_send_1Dat(0x33);lcd_send_1Dat(0x33);//测试
		//lcd_send_1Dat(0x00);lcd_send_1Dat(0x00);
	}
}

/*--------------------------------------------------------------
  * 名称: st7735_init()
  * 传入: 无
  * 返回: 无
  * 功能: 初始化屏幕
  * 说明: 推荐更改为屏幕资料中的初始化指令
----------------------------------------------------------------*/
void st7735_init()
{
	//st7735_Soft_Reset();
	//LCD_RES_Clr();
	//lcd_delay_ms(100);
	//LCD_RES_Set();
	//lcd_delay_ms(100);
	
	#define LCD_WR_REG(x)   lcd_send_1Cmd(x)
	#define LCD_WR_DATA8(x) lcd_send_1Dat(x); 
	//************* Start Initial Sequence **********//
	LCD_WR_REG(0x11); //Sleep out 
	lcd_delay_ms(120);              //Delay 120ms 
	//------------------------------------st7735S Frame Rate-----------------------------------------// 
	LCD_WR_REG(0xB1); 
	LCD_WR_DATA8(0x05); 
	LCD_WR_DATA8(0x3C); 
	LCD_WR_DATA8(0x3C); 
	LCD_WR_REG(0xB2); 
	LCD_WR_DATA8(0x05);
	LCD_WR_DATA8(0x3C); 
	LCD_WR_DATA8(0x3C); 
	LCD_WR_REG(0xB3); 
	LCD_WR_DATA8(0x05); 
	LCD_WR_DATA8(0x3C); 
	LCD_WR_DATA8(0x3C); 
	LCD_WR_DATA8(0x05); 
	LCD_WR_DATA8(0x3C); 
	LCD_WR_DATA8(0x3C); 
	//------------------------------------End st7735S Frame Rate---------------------------------// 
	LCD_WR_REG(0xB4); //Dot inversion 
	LCD_WR_DATA8(0x03); 
	//------------------------------------st7735S Power Sequence---------------------------------// 
	LCD_WR_REG(0xC0); 
	LCD_WR_DATA8(0x28); 
	LCD_WR_DATA8(0x08); 
	LCD_WR_DATA8(0x04); 
	LCD_WR_REG(0xC1); 
	LCD_WR_DATA8(0XC0); 
	LCD_WR_REG(0xC2); 
	LCD_WR_DATA8(0x0D); 
	LCD_WR_DATA8(0x00); 
	LCD_WR_REG(0xC3); 
	LCD_WR_DATA8(0x8D); 
	LCD_WR_DATA8(0x2A); 
	LCD_WR_REG(0xC4); 
	LCD_WR_DATA8(0x8D); 
	LCD_WR_DATA8(0xEE); 
	//---------------------------------End st7735S Power Sequence-------------------------------------// 
	LCD_WR_REG(0xC5); //VCOM 
	LCD_WR_DATA8(0x1A); 
	LCD_WR_REG(0x36); //MX, MY, RGB mode 
	{
		//LCD_WR_DATA8(0x00);//方向1
		LCD_WR_DATA8(0xC0);//方向2
		//LCD_WR_DATA8(0x70);//方向3
		//LCD_WR_DATA8(0xA0); //方向4
		//LCD_WR_DATA8(x);//方向/xy镜像自定义
	}
	//------------------------------------st7735S Gamma Sequence---------------------------------// 
	LCD_WR_REG(0xE0); 
	LCD_WR_DATA8(0x04); 
	LCD_WR_DATA8(0x22); 
	LCD_WR_DATA8(0x07); 
	LCD_WR_DATA8(0x0A); 
	LCD_WR_DATA8(0x2E); 
	LCD_WR_DATA8(0x30); 
	LCD_WR_DATA8(0x25); 
	LCD_WR_DATA8(0x2A); 
	LCD_WR_DATA8(0x28); 
	LCD_WR_DATA8(0x26); 
	LCD_WR_DATA8(0x2E); 
	LCD_WR_DATA8(0x3A); 
	LCD_WR_DATA8(0x00); 
	LCD_WR_DATA8(0x01); 
	LCD_WR_DATA8(0x03); 
	LCD_WR_DATA8(0x13); 
	LCD_WR_REG(0xE1); 
	LCD_WR_DATA8(0x04); 
	LCD_WR_DATA8(0x16); 
	LCD_WR_DATA8(0x06); 
	LCD_WR_DATA8(0x0D); 
	LCD_WR_DATA8(0x2D); 
	LCD_WR_DATA8(0x26); 
	LCD_WR_DATA8(0x23); 
	LCD_WR_DATA8(0x27); 
	LCD_WR_DATA8(0x27); 
	LCD_WR_DATA8(0x25); 
	LCD_WR_DATA8(0x2D); 
	LCD_WR_DATA8(0x3B); 
	LCD_WR_DATA8(0x00); 
	LCD_WR_DATA8(0x01); 
	LCD_WR_DATA8(0x04); 
	LCD_WR_DATA8(0x13); 
	//------------------------------------End st7735S Gamma Sequence-----------------------------// 
	LCD_WR_REG(0x3A); //65k mode rgb565
	LCD_WR_DATA8(0x05); 
	st7735_Clear();//清除IC显示缓存
	LCD_WR_REG(0x29); //Display on 
	 
}


#endif
