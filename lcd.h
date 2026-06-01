/*
 *******************************************************************************
 * EE 329 A3 LCD SUPPORT
 *******************************************************************************
 * @file           : lcd.h
 * @brief          : LCD support functions and defintions
 * project         : EE 329 S'25 - Assignment A3
 * authors         : Kelvin Shi - kshi04@calpoly.edu
 * version         : 1.0
 * date            : 2025/04/14
 * compiler        : STM32CubeIDE v.1.12.0 Build: 14980_20230301_1550 (UTC)
 * target          : NUCLEO-L4A6ZG
 * clocks          : 4 MHz MSI to AHB2
 * @attention      : (c) 2023 STMicroelectronics.  All rights reserved.
 *******************************************************************************
**/
#ifndef INC_LCD_H_
#define INC_LCD_H_
#endif /* INC_LCD_H_ */

#include "stm32l4xx_hal.h"

#define RS_PIN GPIO_PIN_1
#define RW_PIN GPIO_PIN_0;
#define ENABLE_PIN GPIO_PIN_3
#define DB4_PIN GPIO_PIN_4
#define DB5_PIN GPIO_PIN_5
#define DB6_PIN GPIO_PIN_6
#define DB7_PIN GPIO_PIN_7
#define BACKLIGHT GPIO_PIN_2
#define UPPER_NIBBLE (GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7)
#define LCD_PORT GPIOD

void LCD_init(void);
void LCD_pulse_ENA(void);
void LCD_4b_command(uint8_t);
void LCD_command(uint8_t);
void LCD_write_char(uint8_t);
void LCD_ToggleBacklight(uint8_t status);
void LCD_write_string(char* string, uint8_t string_len, uint8_t row);
