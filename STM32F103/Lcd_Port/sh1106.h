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
#ifndef __SH1106_H__
#define __SH1106_H__

#include "stdint.h"

/* SH1106 页式单色 OLED（常见 1.3 寸 128x64；驱动 RAM 132 列，
 * 常规模块列起点偏移 2 —— 由 hw config 的 SCREEN_X_OFFSET 承担）。 */

void sh1106_init(void);
void sh1106_set_addr(uint8_t page, uint8_t col); /* 设页地址 + 列起始（oled_set_addr 宏路由目标） */
void sh1106_set_contrast(uint8_t value);
void sh1106_display_on(void);
void sh1106_display_off(void);

#endif
