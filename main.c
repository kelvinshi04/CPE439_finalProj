/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "app_types.h"

#include "main.h"
#include "cmsis_os.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"

#include "adc.h"
#include "dac.h"
#include "semphr.h"
#include "lcd.h"
#include "keypad.h"
#include "LPUART.h"

#define PRIO_ADC        4
#define PRIO_DAC        4
#define PRIO_METRICS    3
#define PRIO_MODE_MGR   3
#define PRIO_UART       2
#define PRIO_LCD        2

#define STACK_ADC       256
#define STACK_DAC       384
#define STACK_METRICS   512
#define STACK_MODE_MGR  256
#define STACK_UART      384
#define STACK_LCD       256


/* Private variables ---------------------------------------------------------*/
TaskHandle_t adcTaskHandle = NULL;
TaskHandle_t metricsTaskHandle = NULL;
TaskHandle_t dacTaskHandle = NULL;
TaskHandle_t uartTaskHandle = NULL;
TaskHandle_t lcdTaskHandle = NULL;
TaskHandle_t modeManagerTaskHandle = NULL;

QueueHandle_t adcQueue     = NULL;
QueueHandle_t followQueue  = NULL;
QueueHandle_t cmdQueue     = NULL;
QueueHandle_t configQueue  = NULL;
QueueHandle_t lcdQueue     = NULL;
QueueHandle_t uartQueue    = NULL;

SemaphoreHandle_t dataMutex   = NULL;
SemaphoreHandle_t configMutex = NULL;
EventGroupHandle_t eventGroup = NULL;

volatile Measurement_t sharedMeas = {0};

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
extern void UART_task(void *argument);
extern void ADC_task(void *argument);
extern void DAC_task(void *argument);
extern void metrics_task(void *argument);
extern void mode_manager_task(void *argument);
extern void LCD_task(void *argument);

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void) {
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  ADC_init();
  LCD_init();
  DAC_init();
  LPUART_init();

  /* Init scheduler */
  osKernelInitialize();

  /* Initialize FreeRTOS kernel objects for IPC */
  adcQueue    = xQueueCreate(10, sizeof(uint16_t));
  followQueue = xQueueCreate(1,  sizeof(DacSample_t));
  cmdQueue    = xQueueCreate(8,  sizeof(UartCmd_t));
  configQueue = xQueueCreate(1,  sizeof(SystemConfig_t));
  lcdQueue    = xQueueCreate(1,  sizeof(LcdMsg_t));
  dataMutex   = xSemaphoreCreateMutex();
  configMutex = xSemaphoreCreateMutex();
  eventGroup  = xEventGroupCreate();

  xEventGroupClearBits(eventGroup, EV_ALL_MODES | EV_RUNNING);
  xEventGroupSetBits(eventGroup, EV_MODE_DC);

  // Check if any kernel object creation problems
  if (!adcQueue || !followQueue || !cmdQueue || !configQueue ||
	  !lcdQueue || !dataMutex || !configMutex || !eventGroup)
	  return -1;

  //Initial
  SystemConfig_t defaultCfg = {
          .mode      = MODE_DC,
          .wave      = WAVE_SINE,
          .freqHz    = DEFAULT_FREQ_HZ,
          .ampMv     = DEFAULT_AMP_MV,
          .offsetMv  = DEFAULT_OFFSET_MV,
          .phaseStep = (DEFAULT_FREQ_HZ * LUT_SIZE) / SAMPLE_RATE_HZ,
          .running   = 0,
      };
  xQueueSend(configQueue, &defaultCfg, 0);

  /* Create the thread(s) */
  xTaskCreate(ADC_task, "ADC", STACK_ADC, NULL, PRIO_ADC, &adcTaskHandle);
  xTaskCreate(DAC_task, "DAC", STACK_DAC, NULL, PRIO_DAC, &dacTaskHandle);
  xTaskCreate(metrics_task, "Metrics", STACK_METRICS, NULL, PRIO_METRICS, &metricsTaskHandle);
  xTaskCreate(mode_manager_task, "ModeMgr", STACK_MODE_MGR, NULL, PRIO_MODE_MGR, &modeManagerTaskHandle);
  BaseType_t ret = xTaskCreate(UART_task, "UART", STACK_UART, NULL, PRIO_UART, &uartTaskHandle);
  xTaskCreate(LCD_task, "LCD", STACK_LCD, NULL, PRIO_LCD, &lcdTaskHandle);


  if (ret != pdPASS) {
      /* Breakpoint or LED toggle here — heap exhausted */
      while(1) {}
  }


  /* Start scheduler */
  osKernelStart();
  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  while (1){
  }
}

void vApplicationMallocFailedHook(void) {
    /* Set a breakpoint here or toggle an LED */
    __BKPT(0);
    while (1) {}
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
 void SystemClock_Config(void){
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance == TIM6) {
    HAL_IncTick();
  }
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void){
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1){
  }
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
