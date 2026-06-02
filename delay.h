/*
 * delay.h
 *
 *  Created on: Jun 1, 2026
 *      Author: puppe
 */

#ifndef INC_DELAY_H_
#define INC_DELAY_H_

#include "stm32l4xx_hal.h"

void DWT_Init(void);
void delay_us(const uint32_t time_us);

#endif /* INC_DELAY_H_ */
