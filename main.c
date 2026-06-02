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
#include "main.h"
#include "cmsis_os.h"
#include "queue.h"
#include "adc.h"
#include "dac.h"
#include "semphr.h"
#include "lcd.h"

typedef struct {
		uint16_t min;
		uint16_t max;
		uint16_t avg;
} metrics_t;

typedef struct {
		metrics_t data;
		uint16_t dac_code;
} shared_data_t;


/* Private variables ---------------------------------------------------------*/
TaskHandle_t adcTask;
osThreadId_t adcSenseTaskHandle, metricsTaskHandle, dacOutputTaskHandle;
osThreadId_t lcdDisplayTaskHandle, uartTerminalTaskHandle;
QueueHandle_t adcQueue = NULL;
QueueHandle_t gainQueue = NULL;
QueueHandle_t metricsQueue = NULL;
SemaphoreHandle_t updateLCD;
SemaphoreHandle_t dataMutex;
metrics_t curr_data;

const osThreadAttr_t adcSenseTask_attributes = {
  .name = "adcTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

const osThreadAttr_t dacOutputTask_attributes = {
  .name = "dacTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

const osThreadAttr_t metricsTaskHandle_attributes = {
  .name = "metricsTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

const osThreadAttr_t lcdDisplayTask_attributes = {
  .name = "lcdTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

const osThreadAttr_t uartTerminalTask_attributes = {
  .name = "uartTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void adcSenseTask(void *argument);
void metricTask(void *argument);
void dacOutputTask(void *argument);
void lcdDisplayTask(void *argument);
void uartTerminalTask(void *argument);

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

  /* Init scheduler */
  osKernelInitialize();

  updateLCD = xSemaphoreCreateBinary();
  dataMutex = xSemaphoreCreateMutex();

  metricsQueue = xQueueCreate(1, sizeof(metrics_t));
  adcQueue = xQueueCreate(10, sizeof(uint16_t));
  gainQueue = xQueueCreate(1, sizeof(uint8_t));
  // if queues is bad, terminate the program
  if (adcQueue == 0 || gainQueue == 0 || metricsQueue == 0){
	  return -1;
  }


  /* Create the thread(s) */
  adcSenseTaskHandle = osThreadNew(adcSenseTask, NULL, &adcSenseTask_attributes);
  adcTask = (TaskHandle_t) adcSenseTaskHandle;
//  metricsTaskHandle = osThreadNew(metricTask, NULL, &metricsTaskHandle_attributes);
//  dacOutputTaskHandle = osThreadNew(dacOutputTask, NULL, &dacOutputTask_attributes);
//  lcdDisplayTaskHandle = osThreadNew(lcdDisplayTask, NULL, &lcdDisplayTask_attributes);
//  uartTerminalTaskHandle = osThreadNew(uartTerminalTask, NULL, &uartTerminalTask_attributes);

  /* Start scheduler */
  osKernelStart();
  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  while (1){
  }
}


/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
void adcSenseTask(void *argument){
	TickType_t xLastWakeUpTime;
	uint16_t adcData;
	uint16_t intADCData;

	// Initial Last Wake Up Time
	xLastWakeUpTime = xTaskGetTickCount();

	/* Infinite loop */
	for(;;){
		vTaskDelayUntil(&xLastWakeUpTime, pdMS_TO_TICKS(10));
		ADC_startConversion();
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		adcData = ADC_read();
		xQueueSend(adcQueue, &adcData, (TickType_t) 0);
		LCD_write_string("ADC OUTPUT: ", 12, 1);
		LCD_command(0xC0);
		uint32_t mv = ((uint32_t) adcData)*3300U/4095U;
		LCD_write_char('0' + (mv / 1000U));
		LCD_write_char('.');
		LCD_write_char('0' + ((mv / 100U) % 10U));
		LCD_write_char('0' + ((mv / 10U) % 10U));
		LCD_write_char('0' + (mv % 10U));
		osDelay(2000);
  }
}

void metricTask(void *argument){
	uint16_t samp[16];
	uint16_t adcReading;
	uint8_t index = 0;
	uint16_t averg, curr;
	uint16_t lower, upper;
	uint32_t accum;
	metrics_t metrics;

	/* Infinite loop */
	for(;;){
		xQueueReceieve(adcQueue, &adcReading, portMAX_DELAY);
		samp[index%16] = adcReading;

		averg = 0;
		accum = 0;
		for (int i = 1; i < 16; i++){
			curr = samp[i];
			if (curr < lower){
				lower = curr;
			}
			if (curr > upper){
				upper = curr;
			}
			accum += curr;
		}
		averg = accum >> 4;

		xQueueSend(metricsQueue, &metricsQueue, (TickType_t) 0);
	}
}

/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
void dacOutputTask(void *argument){
	uint16_t adcReading, scaledReading, dacCode;
	uint8_t gain, newGain;

	gain = 1;
	/* Infinite loop */
	for(;;){
		xQueueReceieve(adcQueue, &adcReading, portMAX_DELAY);
		if (xQueueReceive(gainQueue, &newGain, (TickType_t) 0) == pdTRUE){
			gain = newGain;
		}

		switch (gain){
			case 0:
				scaledReading = adcReading >> 1;
				break;

			case 2:
				scaledReading = adcReading << 1;
				break;
			default:
				scaledReading = adcReading;
		}
		dacCode = DAC_volt_conv(scaledReading);
		DAC_write(dacCode);

		xSemaphoreTake(dataMutex, portMAX_DELAY);
		xSemaphoreGive(dataMutex);

		xSemaphoreGive(updateLCD);
	}
}


/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
//void lcdDisplayTask(void *argument){
//	display_state_t state;
//	/* Infinite loop */
//	for(;;){
//		xSemaphoreTake(updateLCD, portMAX_DELAY);
//
//		xSemaphoreTake(dataMutex, portMAX_DELAY);
//		state = display_state;
//		xSemaphoreGive(dataMutex);
//
//		LCD_write_string("GAIN: ", 6, 1);
//		if (display.gain == 0){
//			LCD_write_string("0.5x", 4, 1);
//		}
//		else if (display.gain == 1){
//			LCD_write_string("1x  ", 4, 1);
//		}
//		else{
//			LCD_write_string("2x  ", 4, 1);
//		}
//
//		LCD_write_string("ADC: ", 5, 2);
//
//
//  }
//}

/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
void uartTerminalTask(void *argument){
  /* Infinite loop */
  for(;;){
    osDelay(1);
  }
}



/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 71;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV6;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
//static void MX_LPUART1_UART_Init(void)
//{
//
//  /* USER CODE BEGIN LPUART1_Init 0 */
//
//  /* USER CODE END LPUART1_Init 0 */
//
//  /* USER CODE BEGIN LPUART1_Init 1 */
//
//  /* USER CODE END LPUART1_Init 1 */
//  hlpuart1.Instance = LPUART1;
//  hlpuart1.Init.BaudRate = 209700;
//  hlpuart1.Init.WordLength = UART_WORDLENGTH_7B;
//  hlpuart1.Init.StopBits = UART_STOPBITS_1;
//  hlpuart1.Init.Parity = UART_PARITY_NONE;
//  hlpuart1.Init.Mode = UART_MODE_TX_RX;
//  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
//  hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
//  hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
//  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
//  {
//    Error_Handler();
//  }
//  /* USER CODE BEGIN LPUART1_Init 2 */
//
//  /* USER CODE END LPUART1_Init 2 */
//
//}

/**
  * @brief USB_OTG_FS Initialization Function
  * @param None
  * @retval None
  */
/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  HAL_PWREx_EnableVddIO2();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LD3_Pin|LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(USB_PowerSwitchOn_GPIO_Port, USB_PowerSwitchOn_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD3_Pin LD2_Pin */
  GPIO_InitStruct.Pin = LD3_Pin|LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_OverCurrent_Pin */
  GPIO_InitStruct.Pin = USB_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USB_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = USB_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USB_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
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
