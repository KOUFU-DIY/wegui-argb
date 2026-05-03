/**************************************************************************//**
 * @file     FlashPrg.c
 * @brief    Flash Programming Functions adapted for New Device Flash
 * @version  V1.0.0
 * @date     10. January 2018
 ******************************************************************************/
/*
 * Copyright (c) 2010-2018 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
#include "FlashOS.H"        // FlashOS Structures
#include "w25qxx.h"

unsigned long base_adr;        // Base Address
#define bufsize  2048
unsigned char aux_buf[bufsize];//临时变量 越大越快

/* 
   Mandatory Flash Programming Functions (Called by FlashOS):
                int Init        (unsigned long adr,   // Initialize Flash
                                 unsigned long clk,
                                 unsigned long fnc);
                int UnInit      (unsigned long fnc);  // De-initialize Flash
                int EraseSector (unsigned long adr);  // Erase Sector Function
                int ProgramPage (unsigned long adr,   // Program Page Function
                                 unsigned long sz,
                                 unsigned char *buf);

   Optional  Flash Programming Functions (Called by FlashOS):
                int BlankCheck  (unsigned long adr,   // Blank Check
                                 unsigned long sz,
                                 unsigned char pat);
                int EraseChip   (void);               // Erase complete Device
      unsigned long Verify      (unsigned long adr,   // Verify Function
                                 unsigned long sz,
                                 unsigned char *buf);

       - BlanckCheck  is necessary if Flash space is not mapped into CPU memory space
       - Verify       is necessary if Flash space is not mapped into CPU memory space
       - if EraseChip is not provided than EraseSector for all sectors is called
*/


/*
 *  Initialize Flash Programming Functions
 *    Parameter:      adr:  Device Base Address
 *                    clk:  Clock Frequency (Hz)
 *                    fnc:  Function Code (1 - Erase, 2 - Program, 3 - Verify)
 *    Return Value:   0 - OK,  1 - Failed
 */

int Init (unsigned long adr, unsigned long clk, unsigned long fnc) 
{

  /* Add your Code */
	SystemInit();
	base_adr = adr;
	switch(fnc)
	{
		case 1:
			
			flash_port_init();
			break;
		case 2:
			flash_port_init();
			break;
		case 3:
			flash_port_init();
			break;
	}
	//w25qxx_release_powerdown();
  return (0);                                  // Finished without Errors
}


/*
 *  De-Initialize Flash Programming Functions
 *    Parameter:      fnc:  Function Code (1 - Erase, 2 - Program, 3 - Verify)
 *    Return Value:   0 - OK,  1 - Failed
 */

int UnInit (unsigned long fnc) 
{
	/* Add your Code */
	w25qxx_wait_free();
  return (0);                                  // Finished without Errors
}


/*
 *  Erase complete Flash Memory
 *    Return Value:   0 - OK,  1 - Failed
 */

int EraseChip (void) 
{
  /* Add your Code */
	w25qxx_chip_erase();
	w25qxx_wait_free();
  return (0);                                  // Finished without Errors
}


/*
 *  Erase Sector in Flash Memory
 *    Parameter:      adr:  Sector Address
 *    Return Value:   0 - OK,  1 - Failed
 */

int EraseSector (unsigned long adr)
{
  /* Add your Code */
	w25qxx_sector_erase(adr-base_adr);
	w25qxx_wait_free();
  return (0);                                  // Finished without Errors
}


/*
 *  Program Page in Flash Memory
 *    Parameter:      adr:  Page Start Address
 *                    sz:   Page Size
 *                    buf:  Page Data
 *    Return Value:   0 - OK,  1 - Failed
 */

int ProgramPage (unsigned long adr, unsigned long sz, unsigned char *buf) 
{
	
  /* Add your Code */
	w25qxx_write(adr-base_adr, buf, sz);
	w25qxx_wait_free();
  return (0);                                  // Finished without Errors
}





/*- BlankCheck (...) -----------------------------------------------------------
 *
 *  Blank Check Checks if Memory is Blank
 *    Parameter:      adr:  Block Start Address
 *                    sz:   Block Size (in bytes)
 *                    pat:  Block Pattern
 *    Return Value:   0 - OK,  1 - Failed
 */

int BlankCheck (unsigned long adr, unsigned long sz, unsigned char pat) 
{
	volatile int i=0, j=0;
	
  adr -= base_adr;

	for(i = 0; i < sz/bufsize; i++)
	{
		w25qxx_read_data(adr,aux_buf+bufsize*i,bufsize);
		
		for (j = 0; j< bufsize; j++) 
		{
			if (aux_buf[j] != pat) 
			return 1;               /* 需要擦除 */     
		}	
	}
	
	if(sz%bufsize)
	{
		w25qxx_read_data(adr,aux_buf+bufsize*i,bufsize);
		for (j = 0; j< sz%bufsize; j++) 
		{
			if (aux_buf[j] != pat)
			return 1;               /* 需要擦除 */      
		}			
	}
  return 0;
}

unsigned long Verify (unsigned long adr, unsigned long sz, unsigned char *buf)
{
	volatile int i=0, j=0;
  adr -= base_adr;
	for(i = 0; i < sz/bufsize; i++)
	{

		w25qxx_read_data(adr,aux_buf+bufsize*i,bufsize);
		for (j = 0; j< bufsize; j++) 
		{
			if (aux_buf[j] != buf[j+bufsize*i]) 
			return (adr + j + bufsize*i);              /* 校验失败 */     
		}	
	}
	
	if(sz%bufsize)
	{
		w25qxx_read_data(adr,aux_buf+bufsize*i,sz%bufsize);
		for (j = 0; j< sz%bufsize; j++) 
		{
			if (aux_buf[j] != buf[j+bufsize*i]) 
			return (adr + j + bufsize*i);              /* 校验失败 */     
		}			
	}
  adr += base_adr;
  return (adr+sz);                      /* 校验成功 */  
}
