/**
 ******************************************************************************
 * @file    Project/STM32F10x_StdPeriph_Template/stm32f10x_it.c
 * @author  MCD Application Team
 * @version V3.6.0
 * @date    20-September-2021
 * @brief   Main Interrupt Service Routines.
 *          This file provides template for all exceptions handler and
 *          peripherals interrupt service routine.
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

#include "stm32f10x_it.h"

volatile uint8_t  g_sys_tick;
volatile uint32_t g_sys_delay;

/**
 * @brief 不可屏蔽中断处理函数
 */
void NMI_Handler(void)
{
}

/**
 * @brief 硬件错误异常处理函数
 */
void HardFault_Handler(void)
{
    while (1)
    {
    }
}

/**
 * @brief 内存管理异常处理函数
 */
void MemManage_Handler(void)
{
    while (1)
    {
    }
}

/**
 * @brief 总线错误异常处理函数
 */
void BusFault_Handler(void)
{
    while (1)
    {
    }
}

/**
 * @brief 用法错误异常处理函数
 */
void UsageFault_Handler(void)
{
    while (1)
    {
    }
}

/**
 * @brief 系统服务调用异常处理函数
 */
void SVC_Handler(void)
{
}

/**
 * @brief 调试监视异常处理函数
 */
void DebugMon_Handler(void)
{
}

/**
 * @brief 可挂起系统服务异常处理函数
 */
void PendSV_Handler(void)
{
}

/**
 * @brief SysTick 中断处理函数
 * @details 每 1ms 递增系统节拍计数, 并递减软件延时计数器
 */
void SysTick_Handler(void)
{
    if (g_sys_tick < 255U)
    {
        g_sys_tick++;
    }
    if (g_sys_delay > 0U)
    {
        g_sys_delay--;
    }
}
