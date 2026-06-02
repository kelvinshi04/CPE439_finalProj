/*
 * adc.h
 *
 *  Created on: May 31, 2026
 *      Author: kelvi
 */

#ifndef INC_ADC_H_
#define INC_ADC_H_
#endif /* INC_ADC_H_ */

#include "stm32l4xx_hal.h"
#include "cmsis_os.h"

void ADC_init(void);
void ADC_startConversion(void);
void ADC1_2_IRQHandler(void);
uint16_t ADC_read(void);


extern TaskHandle_t adcTask;
