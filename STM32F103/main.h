/*
 * Copyright 2025 Lu Zhihao
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __MAIN_H__
#define __MAIN_H__

#include "stm32f10x.h"
#include "stm32f10x_it.h"
#include "stm32f103_hw_config.h"

/**
 * @brief 毫秒级阻塞延时
 * @param ms 延时时长, 单位毫秒
 */
void delay_ms(uint32_t ms);

#endif
