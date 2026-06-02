/*
*******************************************************************************
* EE 329 A7 LPUART SUPPORT
*******************************************************************************
* @file           : keypad.h
* @brief          : keypad support functions and definitions
* project         : EE 329 S'25 - Assignment A2
* authors         : Kelvin Shi - kshi04@calpoly.edu
* version         : 1.0
* date            : 2025/04/14
* compiler        : STM32CubeIDE v.1.12.0 Build: 14980_20230301_1550 (UTC)
* target          : NUCLEO-L4A6ZG
* clocks          : 4 MHz MSI to AHB2
* @attention      : (c) 2023 STMicroelectronics.  All rights reserved.
*******************************************************************************
**/

#ifndef SRC_UART_H_
#define SRC_UART_H_
#endif /* SRC_UART_H_ */

#include "stm32l4xx_hal.h"

 /* Exported functions prototypes ---------------------------------------------*/
void LPUART_init(void);
void LPUART1_IRQHandler(void);
void LPUART_print(const char* message);
void LPUART_ESC_print( const char* code );

