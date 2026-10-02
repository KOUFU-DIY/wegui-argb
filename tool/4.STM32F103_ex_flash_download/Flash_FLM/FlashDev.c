/**************************************************************************//**
 * @file     FlashDev.c
 * @brief    Flash Device Description for STM32F103 + W25Q128 (SPI2)
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

#include "FlashOS.h"        // FlashOS Structures


struct FlashDevice const FlashDevice  =  {
   FLASH_DRV_VERS,             // Driver Version, do not modify!
   "WE_STM32F103_W25Q128",     // Device Name
   EXTSPI,                     // Device Type
   0x00000000,                 // Device Start Address
   0x01000000,                 // Device Size in Bytes (8MB:0x00800000)(16MB:0x01000000)
   2048,                       // Programming Page Size（调试器一次下发的字节数，算法内部按 256 字节页写入）
   0,                          // Reserved, must be 0
   0xFF,                       // Initial Content of Erased Memory
   10000,                      // Program Page Timeout 10 Sec
   300000,                     // Erase Sector Timeout 300 Sec（整片擦除也用这个时间，16MB 最长约 200 秒）

// Specify Size and Address of Sectors
   {
      { 0x1000, 0x000000 },    // Sector Size  4kB (Sectors)
      { SECTOR_END }
   }
};
