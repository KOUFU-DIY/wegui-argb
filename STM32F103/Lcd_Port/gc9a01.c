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

#if (LCD_IC == _GC9A01)
#include "gc9a01.h"

void gc9a01_set_addr(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	//y2--;//GC9A01需要-1
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

void GC9A01_Soft_Reset()
{
	const uint8_t i[]={0x01};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_Sleep_In()
{
	const uint8_t i[]={0x10};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_Sleep_Out()
{
	const uint8_t i[]={0x11};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_Partial_Mode_On()
{
	const uint8_t i[]={0x12};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_Partial_Mode_Off()
{
	const uint8_t i[]={0x13};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_Inversion_Off()
{
	const uint8_t i[]={0x20};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_Inversion_On()
{
	const uint8_t i[]={0x21};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_ALL_Pixel_Off()
{
	const uint8_t i[]={0x22};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_ALL_Pixel_On()
{
	const uint8_t i[]={0x23};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_Display_Off()
{
	const uint8_t i[]={0x28};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_Display_On()
{
	const uint8_t i[]={0x29};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_TE_Line_Off()
{
	const uint8_t i[]={0x34};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_TE_Line_On()
{
	const uint8_t i[]={0x35,0x01};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_Idle_Off()
{
	const uint8_t i[]={0x38};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_Idle_On()
{
	const uint8_t i[]={0x39};
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));//DMA方式发送命令,在中断接口函数里有数据和命令(DC)切换
}
void GC9A01_Set_RGB565_Mode()
{
	const uint8_t i[]={0x3A,0x55};//65k(RGB565)
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void GC9A01_Set_RGB444_Mode()
{
	const uint8_t i[]={0x3A,0x03};//4k(RGB444)
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}
void GC9A01_Set_RGB666_Mode()
{
	const uint8_t i[]={0x3A,0x06};//262k(RGB666)
	lcd_send_nCmd((uint8_t*)i,sizeof(i)/sizeof(uint8_t));
}




void GC9A01_Clear()//清除IC显示缓存
{
	uint32_t i;
	
	i=0;
	 gc9a01_set_addr(0,0,240-1,240-1);
	while(i++<240*240)
	{
		lcd_send_1Dat(0x33);lcd_send_1Dat(0x33);//测试
		//lcd_send_1Dat(0x00);lcd_send_1Dat(0x00);
	}
	
}


/*--------------------------------------------------------------
  * 名称: gc9a01_init()
  * 传入: 无
  * 返回: 无
  * 功能: 初始化屏幕
  * 说明: 推荐更改为屏幕资料中的初始化指令
----------------------------------------------------------------*/
//void gc9a01_init()
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

void gc9a01_init()
{
	LCD_RES_Set();
	lcd_delay_ms(100);
	LCD_RES_Clr();
	lcd_delay_ms(100);
	LCD_RES_Set();
	lcd_delay_ms(100);



	#define LCD_WR_REG lcd_send_1Cmd
	#define LCD_WR_DATA8 lcd_send_1Dat
	//LCD_BLK_Set();//打开背光
  lcd_delay_ms(100);
	
	LCD_WR_REG(0xEF);
	LCD_WR_REG(0xEB);
	LCD_WR_DATA8(0x14); 
	
  LCD_WR_REG(0xFE);			 
	LCD_WR_REG(0xEF); 

	LCD_WR_REG(0xEB);	
	LCD_WR_DATA8(0x14); 

	LCD_WR_REG(0x84);			
	LCD_WR_DATA8(0x40); 

	LCD_WR_REG(0x85);			
	LCD_WR_DATA8(0xFF); 

	LCD_WR_REG(0x86);			
	LCD_WR_DATA8(0xFF); 

	LCD_WR_REG(0x87);			
	LCD_WR_DATA8(0xFF);

	LCD_WR_REG(0x88);			
	LCD_WR_DATA8(0x0A);

	LCD_WR_REG(0x89);			
	LCD_WR_DATA8(0x21); 

	LCD_WR_REG(0x8A);			
	LCD_WR_DATA8(0x00); 

	LCD_WR_REG(0x8B);			
	LCD_WR_DATA8(0x80); 

	LCD_WR_REG(0x8C);			
	LCD_WR_DATA8(0x01); 

	LCD_WR_REG(0x8D);			
	LCD_WR_DATA8(0x01); 

	LCD_WR_REG(0x8E);			
	LCD_WR_DATA8(0xFF); 

	LCD_WR_REG(0x8F);			
	LCD_WR_DATA8(0xFF); 


	LCD_WR_REG(0xB6);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x20);

	LCD_WR_REG(0x36);
	LCD_WR_DATA8(0x08);
	//LCD_WR_DATA8(0xC8);
	//LCD_WR_DATA8(0x68);
	//LCD_WR_DATA8(0xA8);

	LCD_WR_REG(0x3A);			
	LCD_WR_DATA8(0x05); 


	LCD_WR_REG(0x90);			
	LCD_WR_DATA8(0x08);
	LCD_WR_DATA8(0x08);
	LCD_WR_DATA8(0x08);
	LCD_WR_DATA8(0x08); 

	LCD_WR_REG(0xBD);			
	LCD_WR_DATA8(0x06);
	
	LCD_WR_REG(0xBC);			
	LCD_WR_DATA8(0x00);	

	LCD_WR_REG(0xFF);			
	LCD_WR_DATA8(0x60);
	LCD_WR_DATA8(0x01);
	LCD_WR_DATA8(0x04);

	LCD_WR_REG(0xC3);			
	LCD_WR_DATA8(0x13);
	LCD_WR_REG(0xC4);			
	LCD_WR_DATA8(0x13);

	LCD_WR_REG(0xC9);			
	LCD_WR_DATA8(0x22);

	LCD_WR_REG(0xBE);			
	LCD_WR_DATA8(0x11); 

	LCD_WR_REG(0xE1);			
	LCD_WR_DATA8(0x10);
	LCD_WR_DATA8(0x0E);

	LCD_WR_REG(0xDF);			
	LCD_WR_DATA8(0x21);
	LCD_WR_DATA8(0x0c);
	LCD_WR_DATA8(0x02);

	LCD_WR_REG(0xF0);   
	LCD_WR_DATA8(0x45);
	LCD_WR_DATA8(0x09);
	LCD_WR_DATA8(0x08);
	LCD_WR_DATA8(0x08);
	LCD_WR_DATA8(0x26);
 	LCD_WR_DATA8(0x2A);

 	LCD_WR_REG(0xF1);    
 	LCD_WR_DATA8(0x43);
 	LCD_WR_DATA8(0x70);
 	LCD_WR_DATA8(0x72);
 	LCD_WR_DATA8(0x36);
 	LCD_WR_DATA8(0x37);  
 	LCD_WR_DATA8(0x6F);


 	LCD_WR_REG(0xF2);   
 	LCD_WR_DATA8(0x45);
 	LCD_WR_DATA8(0x09);
 	LCD_WR_DATA8(0x08);
 	LCD_WR_DATA8(0x08);
 	LCD_WR_DATA8(0x26);
 	LCD_WR_DATA8(0x2A);

 	LCD_WR_REG(0xF3);   
 	LCD_WR_DATA8(0x43);
 	LCD_WR_DATA8(0x70);
 	LCD_WR_DATA8(0x72);
 	LCD_WR_DATA8(0x36);
 	LCD_WR_DATA8(0x37); 
 	LCD_WR_DATA8(0x6F);

	LCD_WR_REG(0xED);	
	LCD_WR_DATA8(0x1B); 
	LCD_WR_DATA8(0x0B); 

	LCD_WR_REG(0xAE);			
	LCD_WR_DATA8(0x77);
	
	LCD_WR_REG(0xCD);			
	LCD_WR_DATA8(0x63);		


	LCD_WR_REG(0x70);			
	LCD_WR_DATA8(0x07);
	LCD_WR_DATA8(0x07);
	LCD_WR_DATA8(0x04);
	LCD_WR_DATA8(0x0E); 
	LCD_WR_DATA8(0x0F); 
	LCD_WR_DATA8(0x09);
	LCD_WR_DATA8(0x07);
	LCD_WR_DATA8(0x08);
	LCD_WR_DATA8(0x03);

	LCD_WR_REG(0xE8);			
	LCD_WR_DATA8(0x34);

	LCD_WR_REG(0x62);			
	LCD_WR_DATA8(0x18);
	LCD_WR_DATA8(0x0D);
	LCD_WR_DATA8(0x71);
	LCD_WR_DATA8(0xED);
	LCD_WR_DATA8(0x70); 
	LCD_WR_DATA8(0x70);
	LCD_WR_DATA8(0x18);
	LCD_WR_DATA8(0x0F);
	LCD_WR_DATA8(0x71);
	LCD_WR_DATA8(0xEF);
	LCD_WR_DATA8(0x70); 
	LCD_WR_DATA8(0x70);

	LCD_WR_REG(0x63);			
	LCD_WR_DATA8(0x18);
	LCD_WR_DATA8(0x11);
	LCD_WR_DATA8(0x71);
	LCD_WR_DATA8(0xF1);
	LCD_WR_DATA8(0x70); 
	LCD_WR_DATA8(0x70);
	LCD_WR_DATA8(0x18);
	LCD_WR_DATA8(0x13);
	LCD_WR_DATA8(0x71);
	LCD_WR_DATA8(0xF3);
	LCD_WR_DATA8(0x70); 
	LCD_WR_DATA8(0x70);

	LCD_WR_REG(0x64);			
	LCD_WR_DATA8(0x28);
	LCD_WR_DATA8(0x29);
	LCD_WR_DATA8(0xF1);
	LCD_WR_DATA8(0x01);
	LCD_WR_DATA8(0xF1);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x07);

	LCD_WR_REG(0x66);			
	LCD_WR_DATA8(0x3C);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0xCD);
	LCD_WR_DATA8(0x67);
	LCD_WR_DATA8(0x45);
	LCD_WR_DATA8(0x45);
	LCD_WR_DATA8(0x10);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x00);

	LCD_WR_REG(0x67);			
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x3C);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x01);
	LCD_WR_DATA8(0x54);
	LCD_WR_DATA8(0x10);
	LCD_WR_DATA8(0x32);
	LCD_WR_DATA8(0x98);

	LCD_WR_REG(0x74);			
	LCD_WR_DATA8(0x10);	
	LCD_WR_DATA8(0x85);	
	LCD_WR_DATA8(0x80);
	LCD_WR_DATA8(0x00); 
	LCD_WR_DATA8(0x00); 
	LCD_WR_DATA8(0x4E);
	LCD_WR_DATA8(0x00);					
	
  LCD_WR_REG(0x98);			
	LCD_WR_DATA8(0x3e);
	LCD_WR_DATA8(0x07);

	LCD_WR_REG(0x35);	
	LCD_WR_REG(0x21);

	LCD_WR_REG(0x11);
	lcd_delay_ms(120);
	LCD_WR_REG(0x29);
	lcd_delay_ms(20);
	
	GC9A01_Clear();//清除IC显示缓存
}
#endif
