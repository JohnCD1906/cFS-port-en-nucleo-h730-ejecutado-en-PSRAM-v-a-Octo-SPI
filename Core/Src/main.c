/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file     : main.c
  * @brief    : MAIN APP - cFS Phase 1: OSAL + FatFS structural
  * @target   : STM32H730IBT6Q
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "fatfs.h"
#include "usart.h"
#include "gpio.h"
#include "port_debug.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "libs/uart_debug.h"
#include "osal/osal_freertos.h"
#include "osal_test_task.h"
#include "psp/psp_stm32h730.h"     /* ← NUEVO */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
//static void MPU_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* VTOR a PSRAM: tabla de vectores vive en 0x90000000 (mapeada vía OctoSPI)  */
  SCB->VTOR = 0x90000000UL;
  __enable_irq();
  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  /* MPU deshabilitada durante bring-up cFS. Con caches off no hace falta y
     la config autogenerada de CubeMX bloquea acceso a PSRAM por defecto.
     Se reactivará cuando habilitemos I-cache/D-cache.                         */
  /* MPU_Config(); */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_UART8_Init();
  MX_FATFS_Init();

  /* USER CODE BEGIN 2 */
    PORT_DBG("\n=== cFS on STM32H730 / PSRAM ===\n");

    /* OSAL before the scheduler */
    osKernelInitialize();
    PORT_DBG("osKernelInitialize OK\n");

    if (OS_API_Init() != OS_SUCCESS) {
        OS_printf("MAIN FATAL: OS_API_Init failed\n");
        Error_Handler();
    }
    PORT_DBG("OS_API_Init OK\n");

    /* PSP takes control: mounts RAM disk, runs CFE_ES_Main, creates app tasks */
    PORT_DBG("Launching CFE_PSP_Main...\n");
    CFE_PSP_Main();

    PORT_DBG("Starting scheduler (osKernelStart)\n");
    /* USER CODE END 2 */

  /* Init scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    OS_printf("MAIN FATAL: osKernelStart returned\n");
    Error_Handler();
    while (1)
    {
      /* USER CODE END WHILE */
      /* USER CODE BEGIN 3 */
    }
    /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable */
  HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);

  /** Configure the main internal regulator output voltage */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_CSI|RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = 64;
  RCC_OscInitStruct.CSIState = RCC_CSI_ON;
  RCC_OscInitStruct.CSICalibrationValue = 16;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

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
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* (Sin StartBlinkTask: usamos osal_test_task como en versión previa) */
/* USER CODE END 4 */

 /* MPU Configuration */

/*void MPU_Config(void)
{

}*/

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
  while (1)
  {
  }
}

/* USER CODE BEGIN 5 */
/* FreeRTOS hooks */
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
/* USER CODE END 5 */

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
