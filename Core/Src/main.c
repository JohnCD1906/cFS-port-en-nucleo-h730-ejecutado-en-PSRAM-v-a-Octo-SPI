/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file     : main.c
  * @brief    : MI APP - Bring-up phase 2.1 (FreeRTOS scheduler corriendo)
  * @target   : STM32H730IBT6Q
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "task.h"
#include "libs/uart_debug.h"
#include "gpio.h"
#include "usart.h"
#include "osal/osal_freertos.h"
#include "osal_test_task.h"
/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

/* USER CODE BEGIN PV */
osThreadId_t blinkTaskHandle;
const osThreadAttr_t blinkTask_attributes = {
  .name = "blinkTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE END PV */

void SystemClock_Config(void);

void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */
void StartBlinkTask(void *argument);
/* USER CODE END PFP */

/*int main(void)
{

    SCB->VTOR = 0x90000000UL;

    __enable_irq();


    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_UART8_Init();


    uart_printf("\r\n=== MAIN APP ===\r\n");


    osKernelInitialize();
    uart_printf("[1] osKernelInitialize OK\r\n");

    blinkTaskHandle = osThreadNew(StartBlinkTask, NULL, &blinkTask_attributes);
    if (blinkTaskHandle == NULL) {
        uart_printf("[2] osThreadNew FAIL\r\n");
        Error_Handler();
    }
    uart_printf("[2] osThreadNew OK (handle=%p)\r\n", (void*)blinkTaskHandle);


    osKernelStart();


    uart_printf("[!] osKernelStart retornó — Error_Handler\r\n");
    Error_Handler();
}*/

int main(void)
{
    SCB->VTOR = 0x90000000UL;
    __enable_irq();

    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_UART8_Init();

    uart_printf("\r\n=== MAIN APP - cFS Phase 1: OSAL ===\r\n");

    /* OSAL antes del scheduler */
    osKernelInitialize();
    uart_printf("[1] osKernelInitialize OK\r\n");

    if (OS_API_Init() != OS_SUCCESS) {
        uart_printf("[!] OS_API_Init FAIL\r\n");
        Error_Handler();
    }
    uart_printf("[2] OS_API_Init OK\r\n");

    /* Crear tarea de prueba OSAL */
    osal_test_create();

    /* Arrancar scheduler */
    uart_printf("[3] osKernelStart...\r\n");
    osKernelStart();

    uart_printf("[!] osKernelStart retorno\r\n");
    Error_Handler();
}

/* USER CODE BEGIN 4 */

/* ──────────────────────────────────────────────────────────────────
 * StartBlinkTask — tarea de prueba del scheduler
 * Toggle de LEDs cada 1 s + log por UART con tick count
 * ────────────────────────────────────────────────────────────────── */
void StartBlinkTask(void *argument)
{
    uint32_t tick = 0;
    uart_printf("[blinkTask] Tarea arrancada, scheduler vivo!\r\n");

    for (;;)
    {
        HAL_GPIO_TogglePin(GPIOE, LED1_Pin);
        HAL_GPIO_TogglePin(GPIOE, LED2_Pin);
        uart_printf("[blinkTask] tick %lu (osTick=%lu)\r\n",
                    (unsigned long)tick++,
                    (unsigned long)osKernelGetTickCount());
        osDelay(1000);   /* 1 segundo via FreeRTOS, NO HAL_Delay */
    }
}


/* USER CODE END 4 */

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);
  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_CSI|RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = 64;
  RCC_OscInitStruct.CSIState = RCC_CSI_ON;
  RCC_OscInitStruct.CSICalibrationValue = 16;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
    Error_Handler();
}


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    uart_printf("\r\n!!! STACK OVERFLOW en tarea: %s !!!\r\n", pcTaskName);
    while(1) {
        HAL_GPIO_TogglePin(GPIOE, LED1_Pin);
        HAL_Delay(100);
    }
}

void vApplicationMallocFailedHook(void)
{
    uart_printf("\r\n!!! MALLOC FAILED en FreeRTOS !!!\r\n");
    while(1) {
        HAL_GPIO_TogglePin(GPIOE, LED2_Pin);
        HAL_Delay(100);
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
