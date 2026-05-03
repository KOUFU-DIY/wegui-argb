/**
 ******************************************************************************
 * @file    Project/STM32F10x_StdPeriph_Template/stm32f10x_it.h
 * @author  MCD Application Team
 * @version V3.6.0
 * @date    20-September-2021
 * @brief   This file contains the headers of the interrupt handlers.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2011 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#ifndef __STM32F10x_IT_H
#define __STM32F10x_IT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f10x.h"

/* 系统节拍计数 (SysTick 每 1ms 自增, 主循环清零) */
extern volatile uint8_t  g_sys_tick;
/* 软件阻塞延时计数器 (delay_ms 写入, SysTick 递减) */
extern volatile uint32_t g_sys_delay;

/**
 * @brief 不可屏蔽中断处理函数
 */
void NMI_Handler(void);

/**
 * @brief 硬件错误异常处理函数
 */
void HardFault_Handler(void);

/**
 * @brief 内存管理异常处理函数
 */
void MemManage_Handler(void);

/**
 * @brief 总线错误异常处理函数
 */
void BusFault_Handler(void);

/**
 * @brief 用法错误异常处理函数
 */
void UsageFault_Handler(void);

/**
 * @brief 系统服务调用异常处理函数
 */
void SVC_Handler(void);

/**
 * @brief 调试监视异常处理函数
 */
void DebugMon_Handler(void);

/**
 * @brief 可挂起系统服务异常处理函数
 */
void PendSV_Handler(void);

/**
 * @brief SysTick 中断处理函数
 */
void SysTick_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __STM32F10x_IT_H */
