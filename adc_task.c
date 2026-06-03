/*
 *******************************************************************************
 * task_adc.c
 * ADC_task — highest priority sampler.
 *
 * Flow:
 *   1. ADC_init() configures the peripheral (already bare-metal).
 *   2. ADC_startConversion() kicks off the first conversion.
 *   3. ADC1_2_IRQHandler (in adc.c) calls vTaskNotifyGiveFromISR(adcTask).
 *   4. This task unblocks, reads the result, re-arms, and posts to adcQueue.
 *
 * Design notes:
 *   - xQueueSend with timeout 0 — never block. If adcQueue is full,
 *     metrics_task is behind; drop the sample rather than stall the ADC.
 *   - Do NOT do any arithmetic here. Raw uint16_t only.
 *   - adcTask (extern in adc.h) must be assigned before the scheduler starts.
 *     app_init.c does: adcTask = adcTaskHandle.
 *******************************************************************************
 */

#include "app_types.h"
#include "adc.h"

void ADC_task(void *pvParams) {
    (void)pvParams;
     TickType_t xLastWakeUpTime = xTaskGetTickCount();


    for (;;) {
    	vTaskDelayUntil(&xLastWakeUpTime, pdMS_TO_TICKS(50));

    	ADC_startConversion();
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Read the completed conversion result */
        uint16_t raw = ADC_read();

        xQueueSend(adcQueue, &raw, 0);
    }
}
