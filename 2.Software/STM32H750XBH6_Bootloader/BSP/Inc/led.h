/**
  *************************************************************************************************
  *	@file  	led.h
  *	@brief  LED 驱动（BSP）
  *************************************************************************************************
  *  @description
  *
  *	实验平台：反客STM32H750XBH6核心板 （型号：FK750M5-XBH6）
  *
  *	LED1 使用 PC13：
  *	  - 引脚/时钟/推挽输出等**外设配置**全部由 CubeMX 生成，
  *	    见 Core/Src/gpio.c 的 MX_GPIO_Init()，
  *	    引脚名在 main.h 中：#define LED1_Pin GPIO_PIN_13 / #define LED1_GPIO_Port GPIOC
  *	  - 本文件只提供 CubeMX 生成不了的“语义层”：怎么点亮/熄灭/翻转，
  *	    因此是纯头文件模块，没有对应的 .c
  *
  ************************************************************************************************
**/

#ifndef __LED_H
#define __LED_H

#ifdef __cplusplus
extern "C" {

#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/*------------------------------------------ LED控制宏 ----------------------------------*/

#define LED1_ON      HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET)  /* 输出低电平，点亮LED1 */
#define LED1_OFF     HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET)    /* 输出高电平，关闭LED1 */
#define LED1_Toggle  HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin)                 /* 翻转IO口状态 */

#ifdef __cplusplus
}
#endif

#endif /* __LED_H */
