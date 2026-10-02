#ifndef __STM32F0XX_IT_H
#define __STM32F0XX_IT_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "main.h"

void NMI_Handler(void);
void HardFault_Handler(void);
void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
void DMA1_Channel4_5_IRQHandler(void);

extern volatile uint32_t g_sys_ms; /* 系统毫秒计数 (SysTick 每 1ms 自增, 自然回绕) */
extern volatile uint32_t g_sys_delay;

#ifdef __cplusplus
}
#endif

#endif

