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
#ifndef __SSD1315_H__
#define __SSD1315_H__

#include "stdint.h"

/* SSD1315 页式单色 OLED（SSD1306 兼容型，常见 0.96 寸 128x64）。 */

void ssd1315_init(void);
void ssd1315_set_addr(uint8_t page, uint8_t col); /* 设页地址 + 列起始（oled_set_addr 宏路由目标） */
void ssd1315_set_contrast(uint8_t value);
void ssd1315_display_on(void);
void ssd1315_display_off(void);

#endif
