/**

- @file  psp_stm32f439.c
- @brief PSP — Platform Support Package para STM32F439ZI Nucleo-144
- 
- Diferencias respecto al port F407:
- - RAM disk en SRAM3 (0x20020000) en lugar de SRAM1
- - Heap FreeRTOS ampliado a 64 KB
- - Magic de reset: 0xCAFEF439
- - CPU name: STM32F439ZI-Nucleo
    */

#include "psp_stm32f439.h"
#include "osal_freertos_fs.h"
#include "cfe_default_files.h"
#include "dc_motor_app.h"
#include "cfe_es_stm32.h"

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>
#include <stdio.h>

/* ══════════════════════════════════════════════════════════════════

- TABLA DE APPS ESTATICAS
- ══════════════════════════════════════════════════════════════════ */

const PSP_AppEntry_t PSP_AppTable[] = {
{ "DC_MOTOR_AppMain",  DC_MOTOR_AppMain },
{ NULL,       NULL             }
};

/* ══════════════════════════════════════════════════════════════════

- ESTADO INTERNO
- ══════════════════════════════════════════════════════════════════ */

static volatile PSP_ResetArea_t * const s_reset_area =
(volatile PSP_ResetArea_t *)PSP_RESET_AREA_ADDR;

static uint32 s_boot_type = CFE_PSP_RST_TYPE_POWERON;

/* ══════════════════════════════════════════════════════════════════

- DETECCION DE COLD / WARM START
- ══════════════════════════════════════════════════════════════════ */
  static void psp_detect_boot_type(void)
  {
  if (s_reset_area->magic == PSP_RESET_MAGIC)
  {
  s_boot_type = CFE_PSP_RST_TYPE_PROCESSOR;
  s_reset_area->reset_count++;
  OS_printf("PSP: Warm start detectado (reset #%lu, last_err=%ld)\n",
  (unsigned long)s_reset_area->reset_count,
  (long)s_reset_area->last_error);
  }
  else
  {
  s_boot_type = CFE_PSP_RST_TYPE_POWERON;
  s_reset_area->magic        = PSP_RESET_MAGIC;
  s_reset_area->boot_type    = CFE_PSP_RST_TYPE_POWERON;
  s_reset_area->reset_count  = 0u;
  s_reset_area->last_error   = 0;
  memset((void *)s_reset_area->last_app, 0,
  sizeof(s_reset_area->last_app));
  OS_printf("PSP: Cold start detectado (primer encendido)\n");
  }
  s_reset_area->boot_type = s_boot_type;
  }

/* ══════════════════════════════════════════════════════════════════

- PSP_FS_Init
- 
- El RAM disk esta en SRAM3 (0x20020000, 48 KB).
- SRAM3 es una region separada del heap de FreeRTOS — sin colision.
- ══════════════════════════════════════════════════════════════════ */
  int32 ret;
  int32 PSP_FS_Init(void)
  {

  
  OS_printf("PSP: Inicializando filesystem (RAM disk %u KB en SRAM3)…\n",
  (unsigned)(PSP_RAMDISK_SIZE / 1024u));
  
  ret = OS_FS_Init();
  if (ret != OS_SUCCESS)
  {
  OS_printf("PSP ERROR: OS_FS_Init fallo (%ld)\n", (long)ret);
  return OS_ERROR;
  }
  
  /* Siempre formatear en cold start, intentar montar en warm */
  //int cold = (s_boot_type == CFE_PSP_RST_TYPE_POWERON);
  int cold = 1;
  ret = OS_FS_Mount("0:/", cold);
  if (ret != OS_SUCCESS && !cold)
  {
  OS_printf("PSP WARN: warm mount fallo, re-formateando…\n");
  ret = OS_FS_Mount("0:/", 1);
  }
  if (ret != OS_SUCCESS)
  {
  OS_printf("PSP ERROR: no se pudo montar el RAM disk\n");
  return OS_ERROR;
  }
  
  ret = OS_mkdir("/cf", 0);
  if (ret != OS_SUCCESS)
  {
  OS_printf("PSP ERROR: no se pudo crear /cf\n");
  return OS_ERROR;
  }
  
  ret = OS_FS_WriteFile("/cf/startup.scr",
  startup_scr,
  (uint32)startup_scr_len);
  if (ret != OS_SUCCESS)
  {
  OS_printf("PSP ERROR: no se pudo escribir startup.scr\n");
  return OS_ERROR;
  }
  OS_printf("PSP: /cf/cfe_es_startup.scr escrito (%u bytes)\n",
  (unsigned)startup_scr_len);
  
  ret = OS_FS_WriteFile("/cf/dc_motor.tbl",
  cfe_dc_motor_tbl,
  (uint32)cfe_dc_motor_tbl_len);
  if (ret == OS_SUCCESS)
  OS_printf("PSP: /cf/dc_motor.tbl escrito (%u bytes)\n",
  (unsigned)cfe_dc_motor_tbl_len);
  
  OS_printf("PSP: Filesystem listo.\n");
  return OS_SUCCESS;
  }

/* ══════════════════════════════════════════════════════════════════

- CFE_PSP_Main
- ══════════════════════════════════════════════════════════════════ */
  void CFE_PSP_Main(void)
  {
  OS_printf("\n");
  OS_printf("PSP STM32F439ZI – cFS Port v0.1\n");
  OS_printf("CPU: %s\n", CFE_PSP_CPU_NAME);
  OS_printf("Spacecraft ID: %u\n", (unsigned)CFE_PSP_SPACECRAFT_ID);
  
  psp_detect_boot_type();
  
  if (PSP_FS_Init() != OS_SUCCESS)
  {
  OS_printf("PSP FATAL: filesystem no disponible\n");
  CFE_PSP_Panic(-1);
  return;
  }
  
  OS_printf("PSP: Registrando apps…\n");
  for (int i = 0; PSP_AppTable[i].name != NULL; i++)
  OS_printf("PSP:   [%d] %s\n", i, PSP_AppTable[i].name);
  
  //DC_MOTOR_AppMain();
  CFE_ES_Main(s_boot_type, 0, 0, CFE_PLATFORM_ES_NONVOL_STARTUP_FILE);
  OS_printf("PSP: Inicializacion completa.\n");
  }

/* ══════════════════════════════════════════════════════════════════

- IDENTIFICACION
- ══════════════════════════════════════════════════════════════════ */
  uint32 CFE_PSP_GetProcessorId(void)   { return CFE_PSP_CPU_ID; }
  uint32 CFE_PSP_GetSpacecraftId(void)  { return CFE_PSP_SPACECRAFT_ID; }
  uint32 CFE_PSP_GetBootType(void)      { return s_boot_type; }
  const char *CFE_PSP_GetProcessorName(void) { return CFE_PSP_CPU_NAME; }

/* ══════════════════════════════════════════════════════════════════

- REGIONES DE MEMORIA
- ══════════════════════════════════════════════════════════════════ */
  int32 CFE_PSP_GetVolatileDiskMem(uint32 *addr, uint32 *size)
  {
  if (!addr || !size) return OS_ERROR;
  *addr = PSP_CFE_RAM_ADDR;
  *size = PSP_CFE_RAM_SIZE;
  return OS_SUCCESS;
  }

int32 CFE_PSP_GetResetArea(uint32 *addr, uint32 *size)
{
if (!addr || !size) return OS_ERROR;
*addr = PSP_RESET_AREA_ADDR;
*size = PSP_RESET_AREA_SIZE;
return OS_SUCCESS;
}

int32 CFE_PSP_GetUserReservedArea(uint32 *addr, uint32 *size)
{
if (!addr || !size) return OS_ERROR;
*addr = PSP_USER_AREA_ADDR;
*size = PSP_USER_AREA_SIZE;
return OS_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════

- TIEMPO
- ══════════════════════════════════════════════════════════════════ */
  void CFE_PSP_GetTime(uint32 *seconds, uint32 *microseconds)
  {
  if (!seconds || !microseconds) return;
  uint32 ticks    = (uint32)xTaskGetTickCount();
  *seconds        = ticks / 1000u;
  *microseconds   = (ticks % 1000u) * 1000u;
  }

uint32 CFE_PSP_GetTimerTicksPerSecond(void) { return 1000u; }

/* ══════════════════════════════════════════════════════════════════

- RESET Y PANIC
- ══════════════════════════════════════════════════════════════════ */
  void CFE_PSP_Panic(int32 error_code)
  {
  if (s_reset_area->magic == PSP_RESET_MAGIC)
  s_reset_area->last_error = error_code;

  OS_printf("PSP PANIC: error_code=%ld – reiniciando…\n", (long)error_code);
  HAL_Delay(100);
  HAL_NVIC_SystemReset();
  while(1) {}
  }

void CFE_PSP_Restart(uint32 reset_type)
{
if (s_reset_area->magic == PSP_RESET_MAGIC)
{
s_reset_area->boot_type = reset_type;
if (reset_type == CFE_PSP_RST_TYPE_POWERON)
s_reset_area->magic = 0u;
}
OS_printf("PSP: Restart tipo=%lu\n", (unsigned long)reset_type);
HAL_Delay(100);
HAL_NVIC_SystemReset();
while(1) {}
}

/* ══════════════════════════════════════════════════════════════════

- LOOKUP DE APPS
- ══════════════════════════════════════════════════════════════════ */
  osal_task_entry_t CFE_PSP_FindAppEntry(const char *name)
  {
  if (!name) return NULL;
  for (int i = 0; PSP_AppTable[i].name != NULL; i++)
  if (strncmp(PSP_AppTable[i].name, name, OS_MAX_NAME_LEN) == 0)
  return PSP_AppTable[i].entry_point;
  return NULL;
  }
