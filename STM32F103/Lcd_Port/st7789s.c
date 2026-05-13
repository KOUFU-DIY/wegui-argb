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

#if (LCD_IC == _ST7789S)
#include "st7789s.h"

void st7789s_set_addr(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
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

void st7789s_clear()//清除IC显示缓存
{
	uint32_t i;

//	st7789s_set_addr(0,0,320-1,480-1);
//	i=0;
//	while(i++<240*320)
//	{
//		//lcd_send_1Dat(0x33);lcd_send_1Dat(0x33);//测试
//		lcd_send_1Dat(0x00);lcd_send_1Dat(0x00);
//	}
//

	st7789s_set_addr(0,0,320-1,320-1);
	i=0;
	while(i++<320*320)
	{
		lcd_send_1Dat(0x78);lcd_send_1Dat(0x78);//测试
		//lcd_send_1Dat(0x00);lcd_send_1Dat(0x00);
	}

}





/*--------------------------------------------------------------
  * 名称: st7789s_init()
  * 传入: 无
  * 返回: 无
  * 功能: 初始化屏幕
  * 说明: 推荐更改为屏幕资料中的初始化指令
----------------------------------------------------------------*/
void st7789s_init()
{
	LCD_RES_Clr();
	lcd_delay_ms(100);
	LCD_RES_Set();
	lcd_delay_ms(100);
	
	#define TFT_SEND_CMD lcd_send_1Cmd
	#define TFT_SEND_DATA lcd_send_1Dat
	
	
  TFT_SEND_CMD(0x11); 			//Sleep Out
	lcd_delay_ms(120);               //DELAY120ms 
	//--------------------------------ST7789S Frame rate setting----------------------------------// 
	TFT_SEND_CMD(0x2a); 		//Column address set
	TFT_SEND_DATA(0x00); 		//start column
	TFT_SEND_DATA(0x00); 
	TFT_SEND_DATA(0x00);		//end column
	TFT_SEND_DATA(0xef);

	TFT_SEND_CMD(0x2b); 		//Row address set
	TFT_SEND_DATA(0x00); 		//start row
	TFT_SEND_DATA(0x28); 
	TFT_SEND_DATA(0x01);		//end row
	TFT_SEND_DATA(0x17);

	TFT_SEND_CMD(0xb2); 		//Porch control
	TFT_SEND_DATA(0x0c); 
	TFT_SEND_DATA(0x0c); 
	TFT_SEND_DATA(0x00); 
	TFT_SEND_DATA(0x33); 
	TFT_SEND_DATA(0x33); 

	TFT_SEND_CMD(0x20); 		//Display Inversion Off

	TFT_SEND_CMD(0xb7); 		//Gate control
	TFT_SEND_DATA(0x56);   		//35
//---------------------------------ST7789S Power setting--------------------------------------// 
	TFT_SEND_CMD(0xbb); //VCOMS Setting
	TFT_SEND_DATA(0x18);  //1f

	TFT_SEND_CMD(0xc0); 		//LCM Control
	TFT_SEND_DATA(0x2c); 

	TFT_SEND_CMD(0xc2); 		//VDV and VRH Command Enable
	TFT_SEND_DATA(0x01); 

	TFT_SEND_CMD(0xc3); //VRH Set
	TFT_SEND_DATA(0x1f); //12

	TFT_SEND_CMD(0xc4); 			//VDV Setting
	TFT_SEND_DATA(0x20); 

	TFT_SEND_CMD(0xc6); 			//FR Control 2
	TFT_SEND_DATA(0x0f); 
//TFT_SEND_CMD(0xca); 
//TFT_SEND_DATA(0x0f); 
//TFT_SEND_CMD(0xc8); 
//TFT_SEND_DATA(0x08); 
//TFT_SEND_CMD(0x55); 
//TFT_SEND_DATA(0x90); 
	TFT_SEND_CMD(0xd0);  //Power Control 1
	TFT_SEND_DATA(0xa6);   //a4
	TFT_SEND_DATA(0xa1); 
//--------------------------------ST7789S gamma setting---------------------------------------// 

	TFT_SEND_CMD(0xe0); 
	TFT_SEND_DATA(0xd0); 
	TFT_SEND_DATA(0x0d); 
	TFT_SEND_DATA(0x14); 
	TFT_SEND_DATA(0x0b); 
	TFT_SEND_DATA(0x0b); 
	TFT_SEND_DATA(0x07); 
	TFT_SEND_DATA(0x3a);  
	TFT_SEND_DATA(0x44); 
	TFT_SEND_DATA(0x50); 
	TFT_SEND_DATA(0x08); 
	TFT_SEND_DATA(0x13); 
	TFT_SEND_DATA(0x13); 
	TFT_SEND_DATA(0x2d); 
	TFT_SEND_DATA(0x32); 

	TFT_SEND_CMD(0xe1); 				//Negative Voltage Gamma Contro
	TFT_SEND_DATA(0xd0); 
	TFT_SEND_DATA(0x0d); 
	TFT_SEND_DATA(0x14); 
	TFT_SEND_DATA(0x0b); 
	TFT_SEND_DATA(0x0b); 
	TFT_SEND_DATA(0x07); 
	TFT_SEND_DATA(0x3a); 
	TFT_SEND_DATA(0x44); 
	TFT_SEND_DATA(0x50); 
	TFT_SEND_DATA(0x08); 
	TFT_SEND_DATA(0x13); 
	TFT_SEND_DATA(0x13); 
	TFT_SEND_DATA(0x2d); 
	TFT_SEND_DATA(0x32);
	
	TFT_SEND_CMD(0x36); 			//Memory data access control
	TFT_SEND_DATA(0x00); 
	
	TFT_SEND_CMD(0x3A); 			//Interface pixel format
	TFT_SEND_DATA(0x55);			//65K	
	//TFT_SEND_DATA(0x66);			//262K  RGB 6 6 6

	TFT_SEND_CMD(0xe7); 			//SPI2 enable    启用2数据通道模式
	TFT_SEND_DATA(0x00); 


	TFT_SEND_CMD(0x21);			//Display inversion on
	TFT_SEND_CMD(0x29); 			//Display on
	st7789s_clear();
}

#endif
