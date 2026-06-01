/*
 * dac.h
 *
 *  Created on: May 31, 2026
 *      Author: kelvi
 */

#ifndef INC_DAC_H_
#define INC_DAC_H_
#endif /* INC_DAC_H_ */

#include "stm32l4xx_hal.h"

#define VOLTAGE_RAIL 3300
#define CONTROL_BITS 0x3000

void SPI_GPIO_setup(void);
void SPI_init(void);
void DAC_init(void);
uint16_t DAC_volt_conv(float voltage);
void DAC_GPIO_setup(void);
void DAC_update(void);
void DAC_write(uint16_t data);
