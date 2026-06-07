/**
 * @file  psp_stm32h730.c
 * @brief PSP — Platform Support Package para STM32H730IBT6Q (Fase 1 stub)
 *
 * Fase 1: solo banner + detect_boot_type + osal_test_create.
 * Fase 2: añadir PSP_FS_Init() real (monta FatFS en DTCM).
 * Fase 3: CFE_PSP_Main() llamará a CFE_ES_Main() en lugar de osal_test.
 */

#include "psp/psp_stm32h730.h"
#include "osal/osal_freertos.h"
#include "osal_test_task.h"
#include "osal/osal_freertos_fs.h"   /* NUEVO Fase 2 */
#include "cfe_default_files.h"        /* NUEVO Fase 2 */
#include "cfe/cfe_es_stm32.h"   /* NUEVO Fase 3 */
#include "app/dc_motor_app.h"   /* NUEVO Fase 6 */
#include "port_debug.h"   /* PORT_DBG macro */

#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>
#include <stdio.h>

/* ══════════════════════════════════════════════════════════════════
 * TABLA DE APPS ESTATICAS —
 * ══════════════════════════════════════════════════════════════════ */

const PSP_AppEntry_t PSP_AppTable[] = {
    { "DC_MOTOR_AppMain", DC_MOTOR_AppMain },
    { NULL, NULL }   /* sentinel */
};

/* ══════════════════════════════════════════════════════════════════
 * ESTADO INTERNO
 * ══════════════════════════════════════════════════════════════════ */

static volatile PSP_ResetArea_t * const s_reset_area =
    (volatile PSP_ResetArea_t *)PSP_RESET_AREA_ADDR;

static uint32 s_boot_type = CFE_PSP_RST_TYPE_POWERON;

/* ══════════════════════════════════════════════════════════════════
 * DETECCION DE COLD / WARM START
 *
 * Lee el magic del reset area (SRAM D3). Si coincide → warm reset.
 * Si no → cold reset (primer encendido) e inicializa el area.
 * ══════════════════════════════════════════════════════════════════ */
static void psp_detect_boot_type(void)
{
    if (s_reset_area->magic == PSP_RESET_MAGIC)
    {
        s_boot_type = CFE_PSP_RST_TYPE_PROCESSOR;
        s_reset_area->reset_count++;
        PORT_DBG("Warm start (reset #%lu, last_err=%ld)\n",
                 (unsigned long)s_reset_area->reset_count,
                 (long)s_reset_area->last_error);
    }
    else
    {
        s_boot_type = CFE_PSP_RST_TYPE_POWERON;
        s_reset_area->magic       = PSP_RESET_MAGIC;
        s_reset_area->boot_type   = CFE_PSP_RST_TYPE_POWERON;
        s_reset_area->reset_count = 0u;
        s_reset_area->last_error  = 0;
        memset((void *)s_reset_area->last_app, 0,
               sizeof(s_reset_area->last_app));
        PORT_DBG("Cold start (power-on, or D3 SRAM was cold)\n");
    }
    s_reset_area->boot_type = s_boot_type;
}

/* ══════════════════════════════════════════════════════════════════
 * PSP_FS_Init — Fase 2: monta el RAM disk en DTCM y puebla /cf/
 * ══════════════════════════════════════════════════════════════════ */
int32 PSP_FS_Init(void)
{
    int32 ret;

    PORT_DBG("Initializing filesystem (RAM disk %u KB on DTCM @ 0x%08lX)\n",
             (unsigned)(PSP_RAMDISK_SIZE / 1024u),
             (unsigned long)PSP_RAMDISK_ADDR);

    ret = OS_FS_Init();
    if (ret != OS_SUCCESS)
    {
        OS_printf("PSP FATAL: OS_FS_Init failed (%ld)\n", (long)ret);
        return OS_ERROR;
    }

    int cold = 1;
    ret = OS_FS_Mount("0:/", cold);
    if (ret != OS_SUCCESS)
    {
        OS_printf("PSP FATAL: could not mount RAM disk\n");
        return OS_ERROR;
    }

    ret = OS_mkdir("/cf", 0);
    if (ret != OS_SUCCESS)
    {
        OS_printf("PSP FATAL: could not create /cf\n");
        return OS_ERROR;
    }
    PORT_DBG("/cf created\n");

    ret = OS_FS_WriteFile("/cf/startup.scr",
                           startup_scr, startup_scr_len);
    if (ret != OS_SUCCESS)
    {
        OS_printf("PSP FATAL: could not write startup.scr\n");
        return OS_ERROR;
    }
    PORT_DBG("/cf/startup.scr written (%u bytes)\n",
             (unsigned)startup_scr_len);

    ret = OS_FS_WriteFile("/cf/dc_motor.tbl",
                           cfe_dc_motor_tbl, cfe_dc_motor_tbl_len);
    if (ret == OS_SUCCESS)
    {
        PORT_DBG("/cf/dc_motor.tbl written (%u bytes)\n",
                 (unsigned)cfe_dc_motor_tbl_len);
    }

    PORT_DBG("Filesystem ready\n");
    return OS_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_PSP_Main — punto de entrada del PSP
 *
 * Fase 1: orquesta el bring-up minimo y arranca la tarea de prueba.
 * Fase 3: en lugar de osal_test_create() llamara a CFE_ES_Main().
 * ══════════════════════════════════════════════════════════════════ */
void CFE_PSP_Main(void)
{
    PORT_DBG("\n");
    PORT_DBG("===========================================\n");
    PORT_DBG("PSP %s - cFS port\n", CFE_PSP_CPU_NAME);
    PORT_DBG("Spacecraft ID: %u\n", (unsigned)CFE_PSP_SPACECRAFT_ID);
    PORT_DBG("Processor ID:  %u\n", (unsigned)CFE_PSP_CPU_ID);
    PORT_DBG("===========================================\n");

    psp_detect_boot_type();

    if (PSP_FS_Init() != OS_SUCCESS)
    {
        OS_printf("PSP FATAL: filesystem unavailable\n");
        CFE_PSP_Panic(-1);
        return;
    }

    PORT_DBG("PSP_AppTable[]:\n");
    if (PSP_AppTable[0].name == NULL)
    {
        PORT_DBG("  (empty - no apps registered)\n");
    }
    else
    {
        for (int i = 0; PSP_AppTable[i].name != NULL; i++)
            PORT_DBG("  [%d] %s\n", i, PSP_AppTable[i].name);
    }

    PORT_DBG("Handing control to CFE_ES_Main\n");
    CFE_ES_Main(s_boot_type, 0, 0, CFE_PLATFORM_ES_NONVOL_STARTUP_FILE);

    PORT_DBG("CFE_ES_Main returned - init complete\n");
}

/* ══════════════════════════════════════════════════════════════════
 * IDENTIFICACION
 * ══════════════════════════════════════════════════════════════════ */
uint32 CFE_PSP_GetProcessorId(void)         { return CFE_PSP_CPU_ID; }
uint32 CFE_PSP_GetSpacecraftId(void)        { return CFE_PSP_SPACECRAFT_ID; }
uint32 CFE_PSP_GetBootType(void)            { return s_boot_type; }
const char *CFE_PSP_GetProcessorName(void)  { return CFE_PSP_CPU_NAME; }

/* ══════════════════════════════════════════════════════════════════
 * REGIONES DE MEMORIA
 * ══════════════════════════════════════════════════════════════════ */
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
 * TIEMPO
 * ══════════════════════════════════════════════════════════════════ */
void CFE_PSP_GetTime(uint32 *seconds, uint32 *microseconds)
{
    if (!seconds || !microseconds) return;
    uint32 ticks    = (uint32)xTaskGetTickCount();
    *seconds        = ticks / 1000u;
    *microseconds   = (ticks % 1000u) * 1000u;
}

uint32 CFE_PSP_GetTimerTicksPerSecond(void) { return 1000u; }

/* ══════════════════════════════════════════════════════════════════
 * RESET Y PANIC
 * ══════════════════════════════════════════════════════════════════ */
void CFE_PSP_Panic(int32 error_code)
{
    if (s_reset_area->magic == PSP_RESET_MAGIC)
        s_reset_area->last_error = error_code;

    OS_printf("PSP PANIC: error_code=%ld - restarting...\n",
              (long)error_code);
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
    OS_printf("PSP: restart type=%lu\n", (unsigned long)reset_type);
    HAL_Delay(100);
    HAL_NVIC_SystemReset();
    while(1) {}
}


/* ══════════════════════════════════════════════════════════════════
 * LOOKUP DE APPS
 * ══════════════════════════════════════════════════════════════════ */
osal_task_entry_t CFE_PSP_FindAppEntry(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; PSP_AppTable[i].name != NULL; i++)
    {
        if (strncmp(PSP_AppTable[i].name, name, OS_MAX_NAME_LEN) == 0)
            return PSP_AppTable[i].entry_point;
    }
    return NULL;
}
