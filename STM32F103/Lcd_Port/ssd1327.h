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
#ifndef __SSD1327_H__
#define __SSD1327_H__

#include "stdint.h"

/* SSD1327 灰度 OLED（128x128，16 级灰度，1 字节 = 水平 2 像素）
 *
 * 与页式单色 OLED 不同：有真正的矩形窗口（0x15 列 / 0x75 行）且窗口内
 * 自动递增，因此 set_addr 直接把窗口发给 IC，flush 只做 GRAY8 -> 4bpp
 * 双像素打包后流式发送。
 * 列地址以 2 像素为单位 —— 依赖 we_user_config.h 的 WE_LCD_FLUSH_ALIGN_X = 2。 */

void ssd1327_init(void);
void ssd1327_set_addr(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void ssd1327_set_contrast(uint8_t value);
void ssd1327_display_on(void);
void ssd1327_display_off(void);

#endif
