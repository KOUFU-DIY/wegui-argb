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

#if (LCD_IC == _ST7796S)
#include "st7796s.h"

void st7796s_set_addr(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
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

void ST7796S_Soft_Reset()
{
	const uint8_t i[]={0x01};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_Sleep_In()
{
	//Sleep out
	const uint8_t i[]={0x10};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_Sleep_Out()
{
	//Sleep out
	const uint8_t i[]={0x11};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_Partial_Mode_On()
{
	const uint8_t i[]={0x12};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_Partial_Mode_Off()
{
	const uint8_t i[]={0x13};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_Inversion_Off()
{
	const uint8_t i[]={0x20};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_Inversion_On()
{
	const uint8_t i[]={0x21};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_Display_Off()
{
	const uint8_t i[]={0x28};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_Display_On()
{
	const uint8_t i[]={0x29};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_TE_Line_Off()
{
	const uint8_t i[]={0x34};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_TE_Line_On()
{
	#define TELOM 1//[0:1]When TELOM =’0’: The Tearing Effect output line consists of V-Blanking information only
	const uint8_t i[]={0x35,TELOM};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_Idle_Off()
{
	const uint8_t i[]={0x38};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_Idle_On()
{
	const uint8_t i[]={0x39};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_Set_RGB565_Mode()
{
	uint8_t i[]={0x3A,0x05};//65k(RGB565)
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_Set_RGB666_Mode()
{
	uint8_t i[]={0x3A,0x06};//262k(RGB666)
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void ST7796S_Set_RGB888_Mode()
{
	uint8_t i[]={0x3A,0x07};//16M(RGB888)
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}

void ST7796S_Clear()//清除IC显示缓存
{
	uint32_t i;
	st7796s_set_addr(0,0,480-1,480-1);
	i=0;
	while(i++<480*480)
	{
		lcd_send_1Dat(0x33);lcd_send_1Dat(0x33);//测试
		//lcd_send_1Dat(0x00);lcd_send_1Dat(0x00);
	}
	
}
/*--------------------------------------------------------------
  * 名称: st7796s_init()
  * 传入: 无
  * 返回: 无
  * 功能: 初始化屏幕
  * 说明: 推荐更改为屏幕资料中的初始化指令
----------------------------------------------------------------*/
//自定义初始化示例
//void st7796s_init()
//{
//	ST7735_Soft_Reset();
//	LCD_RES_Clr();
//	LCD_Delay(100);
//	LCD_RES_Set();
//	LCD_Delay(100);
//	
//  lcd_delay_ms(100);
//	
//	lcd_send_1Cmd(0x11);//Sleep exit 
//	lcd_delay_ms(120);
//		
//	//ST7735R Frame Rate
//	lcd_send_1Cmd(0xB1); 
//	lcd_send_1Dat(0x01); 
//	lcd_send_1Dat(0x2C); 
//	lcd_send_1Dat(0x2D); 

//	lcd_send_1Cmd(0xB2); 
//	lcd_send_1Dat(0x01); 
//	lcd_send_1Dat(0x2C); 
//	lcd_send_1Dat(0x2D); 

//	lcd_send_1Cmd(0xB3); 
//	lcd_send_1Dat(0x01); 
//	lcd_send_1Dat(0x2C); 
//	lcd_send_1Dat(0x2D); 
//	lcd_send_1Dat(0x01); 
//	lcd_send_1Dat(0x2C); 
//	lcd_send_1Dat(0x2D); 
//	
//	lcd_send_1Cmd(0xB4); //Column inversion 
//	lcd_send_1Dat(0x07); 
//	
//	//ST7735R Power Sequence
//	lcd_send_1Cmd(0xC0); 
//	lcd_send_1Dat(0xA2); 
//	lcd_send_1Dat(0x02); 
//	lcd_send_1Dat(0x84); 
//	lcd_send_1Cmd(0xC1); 
//	lcd_send_1Dat(0xC5); 

//	lcd_send_1Cmd(0xC2); 
//	lcd_send_1Dat(0x0A); 
//	lcd_send_1Dat(0x00); 

//	lcd_send_1Cmd(0xC3); 
//	lcd_send_1Dat(0x8A); 
//	lcd_send_1Dat(0x2A); 
//	lcd_send_1Cmd(0xC4); 
//	lcd_send_1Dat(0x8A); 
//	lcd_send_1Dat(0xEE); 
//	
//	lcd_send_1Cmd(0xC5); //VCOM 
//	lcd_send_1Dat(0x0E); 
//	
//	//方向选择其一
//	lcd_send_1Cmd(0x36);
//	lcd_send_1Dat(0xC8);//方向1
//	//lcd_send_1Dat(0x08);//方向2
//	//lcd_send_1Dat(0x78);//方向3
//	//lcd_send_1Dat(0xA8);//方向4
//	
//	//ST7735R Gamma Sequence
//	lcd_send_1Cmd(0xe0); 
//	lcd_send_1Dat(0x0f); 
//	lcd_send_1Dat(0x1a); 
//	lcd_send_1Dat(0x0f); 
//	lcd_send_1Dat(0x18); 
//	lcd_send_1Dat(0x2f); 
//	lcd_send_1Dat(0x28); 
//	lcd_send_1Dat(0x20); 
//	lcd_send_1Dat(0x22); 
//	lcd_send_1Dat(0x1f); 
//	lcd_send_1Dat(0x1b); 
//	lcd_send_1Dat(0x23); 
//	lcd_send_1Dat(0x37); 
//	lcd_send_1Dat(0x00); 	
//	lcd_send_1Dat(0x07); 
//	lcd_send_1Dat(0x02); 
//	lcd_send_1Dat(0x10); 

//	lcd_send_1Cmd(0xe1); 
//	lcd_send_1Dat(0x0f); 
//	lcd_send_1Dat(0x1b); 
//	lcd_send_1Dat(0x0f); 
//	lcd_send_1Dat(0x17); 
//	lcd_send_1Dat(0x33); 
//	lcd_send_1Dat(0x2c); 
//	lcd_send_1Dat(0x29); 
//	lcd_send_1Dat(0x2e); 
//	lcd_send_1Dat(0x30); 
//	lcd_send_1Dat(0x30); 
//	lcd_send_1Dat(0x39); 
//	lcd_send_1Dat(0x3f); 
//	lcd_send_1Dat(0x00); 
//	lcd_send_1Dat(0x07); 
//	lcd_send_1Dat(0x03); 
//	lcd_send_1Dat(0x10);  
//	
//	lcd_send_1Cmd(0x2a);
//	lcd_send_1Dat(0x00);
//	lcd_send_1Dat(0x00);
//	lcd_send_1Dat(0x00);
//	lcd_send_1Dat(0x7f);

//	lcd_send_1Cmd(0x2b);
//	lcd_send_1Dat(0x00);
//	lcd_send_1Dat(0x00);
//	lcd_send_1Dat(0x00);
//	lcd_send_1Dat(0x9f);
//	
//	lcd_send_1Cmd(0xF0); //Enable test command  
//	lcd_send_1Dat(0x01); 
//	lcd_send_1Cmd(0xF6); //Disable ram power save mode 
//	lcd_send_1Dat(0x00); 
//	
//	lcd_send_1Cmd(0x3A); //65k mode 
//	lcd_send_1Dat(0x05); 

//	ST7735_Clear();//清除IC显示缓存
//	lcd_send_1Cmd(0x29);//Display on	 
//}

void st7796s_init()    
{
//	LCD_RES_Clr();
//	lcd_delay_ms(100);
//	LCD_RES_Set();
//	lcd_delay_ms(100);
	
	//ST7796S_Soft_Reset();
	ST7796S_Sleep_Out();
	lcd_delay_ms(120);
	
	{
		//CSCON (F0h): Command Set Control
		//使能设置1
		const uint8_t i[]={0xF0,0xC3};//C3h enable command 2 part I
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
	}
	{
		//CSCON (F0h): Command Set Control
		//使能设置2
		const uint8_t i[]={0xF0,0x96};//96h enable command 2 part II
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
	}
	
	{
		//MADCTL (36h): Memory Data Access Control
		//设置显示方向
		#define MADCTL_MY 0//[0:1]Row Address Order
		#define MADCTL_MX 1//[0:1]Column Address Order
		#define MADCTL_MV 0//[0:1]Row/Column Exchange
		#define MADCTL_ML 0//[0:1]Vertical Refresh Order 0="LCD vertical refresh Top to Bottom" 1="LCD vertical refresh Bottom to Top"
		#define MADCTL_RGB 1//[0:1]Color selector switch control 0=RGB 1=BGR
		#define MADCTL_MH 0//[0:1]Horizontal Refresh Order 0="LCD horizontal refresh Left to right" 1="LCD horizontal refresh right to left"
		const uint8_t i[]={0x36,(MADCTL_MY<<7)|(MADCTL_MX<<6)|(MADCTL_MV<<5)|(MADCTL_ML<<4)|(MADCTL_RGB<<3)|(MADCTL_MH<<2)};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
	}
	

	
	
	{
		//FRMCTR1 (B1h): Frame Rate Control (In normal mode/ Full colors)
		
		//默认
		//#define FRMCTR1_FRS 10//[0~31]
		//#define FRMCTR1_DIVA 0//[0~2] 0=FOSC 1=FOSC/2 2=FOSC/4 3=FOSC/8
		//#define FRMCTR1_RTNA 16//[0~31]
		
		//自定义 数值越小, 刷新越快
		#define FRMCTR1_FRS 10//[0~31]
		#define FRMCTR1_DIVA 0//[0~2] 0=FOSC 1=FOSC/2 2=FOSC/4 3=FOSC/8
		#define FRMCTR1_RTNA 16//[0~31]
		
		//刷新率=1/(138*RTNA+(32*15*FRS)*(480+VFP+VBP))
		const uint8_t i[]={0xB1,(FRMCTR1_FRS<<4)|FRMCTR1_DIVA,FRMCTR1_RTNA};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
	}
	/*
	{
		//FRMCTR2 (B2h): Frame Rate Control (In Idle mode/ 8-colors)
		const uint8_t i[]={0xB2,0x00,0x10};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
		while(Get_DMA_Busy_State()!=0){};
	}
	{
		//FRMCTR3 (B3h): Frame Rate Control (In Partial mode/ full colors)
		const uint8_t i[]={0xB3,0x10};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
		while(Get_DMA_Busy_State()!=0){};
	}
	
	{
		//INVCTR (B4h): Display Inversion Control
		//Display inversion control
		//0=Column inversion
		//1= 1-dot inversion
		//2= 2-dot inversion
		const uint8_t i[]={0xB4,0x01};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
		while(Get_DMA_Busy_State()!=0){};
	}
	{
		//BPC(B5): Blanking Porch Control
		const uint8_t i[]={0xB4,0x02,0x02,0x00,0x04};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
		while(Get_DMA_Busy_State()!=0){};
	}
	
	{
		//DFC(B6): Display Function Control
		//行排列模式 控制模式等
		//#define DFC_RM  0 //0="System Interface" 1="RGB interface"
		//#define DFC_RCM 0 //RGB transfer mode 0="DE Mode" 1="SYNC Mode"
		//#define DFC_BYPASS 0 //Display data path 0="Memory" 1="Direct to shift register"
		//#define DFC_PTG 0 //Display data path 0="Memory" 1="Direct to shift register"
		const uint8_t i[]={0xB6,0x80,0x02,0x3B};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
		while(Get_DMA_Busy_State()!=0){};
	}
	
	{
		//EM(B7): Entry Mode Setl
		//行排列模式 控制模式等
		const uint8_t i[]={0xB7,0x06};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
		while(Get_DMA_Busy_State()!=0){};
	}
	{
		//PWR1(C0h): Power Control 1
		const uint8_t i[]={0xC0,0x80,0x25};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
		while(Get_DMA_Busy_State()!=0){};
			
	} 
	
	{
		//Power Control 2
		const uint8_t i[]={0xC1,0x13};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
		while(Get_DMA_Busy_State()!=0){};
	} 

	{
		//Power Control 3 (in Normal mode/ Full colors)
		#define PWR3_SOP 2//[0:3]Source driving current level
		#define PWR3_GOP 2//[0:3]Gamma driving current level
		const uint8_t i[]={0xC2,((PWR3_SOP<<2)|PWR3_GOP)};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
		while(Get_DMA_Busy_State()!=0){};
	}

	{
		//VCM Offset (C6h): Vcom Offset Register
		const uint8_t i[]={0xC6,0x00};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
		while(Get_DMA_Busy_State()!=0){};
	}
	
	//NVMADW (D0h): NVM Address/Data Write
	//NVMBPROG (D1h): NVM Byte Program
	//NVM Status Read(D2h)
	//RDID4 (D3h): Read ID4

	*/

	{
		//GMCTRP1 (E0h): Gamma (‘+’polarity) Correction Characteristics Setting
		uint8_t i[]={0xE0,0xF0,0x03,0x0A,0x11,0x14,0x1C,0x3B,0x55,0x4A,0x0A,0x13,0x14,0x1C,0x1F};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
	}
	{
		//GMCTRN1 (E1h): Gamma ‘-’polarity Correction Characteristics Setting
		uint8_t i[]={0xE1,0xF0,0x03,0x0A,0x0C,0x0C,0x09,0x36,0x54,0x49,0x0F,0x1B,0x18,0x1B,0x1F};
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
	}
	
	
	
	{
		//CSCON (F0h): Command Set Control
		//关闭设置1
		const uint8_t i[]={0xF0,0x3C};//C3h enable command 2 part I
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
	}
	{
		//CSCON (F0h): Command Set Control
		//关闭设置2
		const uint8_t i[]={0xF0,0x69};//96h enable command 2 part II
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
	}
	//DGC1(E2h): Digital Gamma Control 1
	//DGC2 (E3h): Digital Gamma Control 2
	//DOCA (E8h): Display Output Ctrl Adjust
	
	//ST7796S_TE_Line_On();
	ST7796S_Set_RGB565_Mode();
	//ST7796S_Set_RGB666_Mode();
	//ST7796S_Set_RGB888_Mode();
	
	{
		//CSCON (F0h): Command Set Control
		//关闭特殊设置1
		const uint8_t i[]={0xF0,0x3C};//C3h enable command 2 part I
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
	}
	{
		//CSCON (F0h): Command Set Control
		//使能特殊设置2
		const uint8_t i[]={0xF0,0x69};//96h enable command 2 part II
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
	}
	
	ST7796S_Clear();//清除IC显示缓存
	
	
	{
		uint8_t i[]={0x29};//Display on
		lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
	}
}
#endif
