/*******************************************************************************
 * EE 329 A8 ADC FUNCTIONS
 *******************************************************************************
 * @file           : ADC.c
 * @brief          : ADC initialization, interrupt-driven conversion for FreeRTOS
 * project         : EE 329 S'25 - Custom FreeRTOS Project
 * authors         : Kelvin Shi - kshi04@calpoly.edu
 * version         : 2.0
 * date            : 2025/05/31
 * compiler        : STM32CubeIDE v.1.12.0 Build: 14980_20230301_1550 (UTC)
 * target          : NUCLEO-L4A6ZG
 * clocks          : 80 MHz MSI PLL to AHB, ADC @ HCLK/4 = 20 MHz
 * @attention      : (c) 2023 STMicroelectronics.  All rights reserved.
 *******************************************************************************
 * ADC WIRING
 *      peripheral – Nucleo I/O
 *
 * PINOUT:
 * ADC1_IN5 - PA0 - analog input (potentiometer / photoresistor / bench supply)
 *            GND - signal ground (connect before powering source)
 *
 *******************************************************************************
 */
#include "ADC.h"
#include "delay.h"

/* -----------------------------------------------------------------------------
 * function : void ADC_init(void)
 * INs      : none
 * OUTs     : none
 * action   : Initialize ADC1 channel 5 (PA0) for single-ended, single-
 *            conversion, interrupt-driven operation. Calibrates ADC before
 *            enabling. Configures NVIC for use with FreeRTOS task notification.
 *            ADC clock set to HCLK/4 = 20 MHz to stay within 26 MHz spec.
 * authors  : Kelvin Shi - kshi04@calpoly.edu
 * version  : 2.0
 * date     : 2025/05/31
 * -------------------------------------------------------------------------- */
void ADC_init(void) {
   RCC->AHB2ENR |= RCC_AHB2ENR_ADCEN;              // enable ADC peripheral clock

   DWT_Init();

   // power up & calibrate ADC
   ADC123_COMMON->CCR |= (3 << ADC_CCR_CKMODE_Pos); // HCLK/4 = 20 MHz, within 26 MHz spec
   ADC1->CR &= ~(ADC_CR_DEEPPWD);                   // disable deep-power-down mode
   ADC1->CR |= (ADC_CR_ADVREGEN);                   // enable internal voltage regulator
   delay_us(20);                                    // wait 20 us for regulator startup
   ADC1->DIFSEL &= ~(ADC_DIFSEL_DIFSEL_5);          // PA0=ADC1_IN5, single-ended mode
   ADC1->CR &= ~(ADC_CR_ADEN | ADC_CR_ADCALDIF);   // disable ADC, single-ended calib
   ADC1->CR |= ADC_CR_ADCAL;                        // start calibration sequence
   while (ADC1->CR & ADC_CR_ADCAL);                 // wait for calibration to finish

   // enable ADC
   ADC1->ISR |= (ADC_ISR_ADRDY);                   // write 1 to clear ADRDY flag
   ADC1->CR  |= ADC_CR_ADEN;                        // enable ADC
   while(!(ADC1->ISR & ADC_ISR_ADRDY));             // wait until ADC ready
   ADC1->ISR |= (ADC_ISR_ADRDY);                   // write 1 to clear ADRDY flag

   // configure ADC sampling & sequencing
   ADC1->SQR1  |= (5 << ADC_SQR1_SQ1_Pos);         // 1 conversion, channel 5 (PA0)
   ADC1->SMPR1 |= (7 << ADC_SMPR1_SMP5_Pos);       // ch5 sample time = 640.5 clocks
   ADC1->CFGR  &= ~(ADC_CFGR_CONT  |               // single conversion mode (not cont.)
                    ADC_CFGR_EXTEN |               // software trigger (no h/w trigger)
                    ADC_CFGR_RES   );              // 12-bit resolution

   // configure & enable ADC interrupt
   ADC1->IER |= ADC_IER_EOCIE;                      // enable end-of-conversion interrupt
   ADC1->ISR |= ADC_ISR_EOC;                        // write 1 to clear EOC flag
   NVIC->ISER[0] = (1 << (ADC1_2_IRQn & 0x1F));    // enable ADC1_2 in NVIC
   __enable_irq();                                  // enable global interrupts

   // configure PA0 as analog input
   RCC->AHB2ENR  |= (RCC_AHB2ENR_GPIOAEN);         // enable GPIOA clock
   GPIOA->MODER  |= (GPIO_MODER_MODE0);             // PA0 = analog mode (set last)
}

/* -----------------------------------------------------------------------------
 * function : void ADC_startConversion(void)
 * INs      : none
 * OUTs     : none
 * action   : clear EOC flag and trigger a single ADC conversion via software.
 *            call this from ADC_Sensor_Task before blocking on task notification
 * authors  : Kelvin Shi - kshi04@calpoly.edu
 * version  : 2.0
 * date     : 2025/05/31
 * -------------------------------------------------------------------------- */
void ADC_startConversion(void) {
   ADC1->ISR |= ADC_ISR_EOC;                        // write 1 to clear EOC flag
   ADC1->CR  |= ADC_CR_ADSTART;                     // start single conversion
}

/* -----------------------------------------------------------------------------
 * function : void ADC1_2_IRQHandler(void)
 * INs      : none
 * OUTs     : none
 * action   : fires on ADC end-of-conversion. clears EOC flag and sends a
 *            FreeRTOS task notification to adcSenseTaskHandle to unblock
 *            ADC_Sensor_Task. calls portYIELD_FROM_ISR() to yield immediately
 *            if a higher priority task was woken.
 * authors  : Kelvin Shi - kshi04@calpoly.edu
 * version  : 2.0
 * date     : 2025/05/31
 * -------------------------------------------------------------------------- */
void ADC1_2_IRQHandler(void) {
   if (ADC1->ISR & ADC_ISR_EOC) {
      ADC1->ISR |= ADC_ISR_EOC;                     // write 1 to clear EOC flag

      BaseType_t xHigherPriorityTaskWoken = pdFALSE;         // init yield flag
      vTaskNotifyGiveFromISR(adcTask,                       // notify ADC task
                             &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);          // yield if needed
   }
}


/* -----------------------------------------------------------------------------

    function : uint16_t ADC_read(void)
    INs      : none
    OUTs     : uint16 - the data sampled
    action   : read the sampled data from ADC data register
    authors  : Kelvin Shi - kshi04@calpoly.edu
    version  : 2.0
    date     : 2025/05/31
    -------------------------------------------------------------------------- */

uint16_t ADC_read(void){
    return ADC1->DR;
}
