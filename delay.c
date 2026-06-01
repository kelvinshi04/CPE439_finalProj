
#include "delay.h"


/* -----------------------------------------------------------------------------
 * function : void DWT_Init(void)
 * INs      : none
 * OUTs     : none
 * action   : configure DWT peripheral for use with delay_us().
 * authors  : Kelvin Shi - kshi04@calpoly.edu
 * version  : 1.0
 * date     : 2026/05/31
 * -------------------------------------------------------------------------- */
void DWT_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}


/* -----------------------------------------------------------------------------
 * function : void delay_us(const uint32_t)
 * INs      : const uint32_t - the amount of time in us to delay
 * OUTs     : none
 * action   : delay in microseconds using DWT to count CPU clock
 * 			  cycles. DO NOT call with 0 otherwise introduce an error. Small
 * 			  inputs will not reflect real time
 * authors  : Kelvin Shi - kshi04@calpoly.edu
 * version  : 1.0
 * date     : 2026/05/31
 * -------------------------------------------------------------------------- */
void delay_us(const uint32_t time_us) {
	uint32_t start = DWT->CYCCNT;
	uint32_t ticks = time_us * (SystemCoreClock / 1000000);
	while ((DWT->CYCCNT - start) < ticks);
}
