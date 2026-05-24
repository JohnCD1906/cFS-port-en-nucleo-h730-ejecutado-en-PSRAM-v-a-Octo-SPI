/**
 * @file  psp_stm32h730.h
 * @brief PSP — Platform Support Package para STM32H730IBT6Q
 *
 * ── Diferencias H730 vs F439 ──────────────────────────────────
 *
 *   H730 NO tiene CCMRAM ni SRAM3 (esas son F4).
 *   En su lugar usamos:
 *     - DTCM   (128 KB @ 0x20000000) para RAM disk FatFS
 *     - SRAM D2 (32 KB @ 0x30000000) para buffers cFE
 *     - SRAM D3 (16 KB @ 0x38000000) para reset area + user area
 *
 * ── Mapa de memoria ───────────────────────────────────────────
 *
 *   AXI-SRAM D1 (320 KB — 0x24000000)
 *   ┌──────────────────────────────────────────────┐ 0x24000000
 *   │  .data, .bss, heap FreeRTOS, stack           │  ~150 KB
 *   │  (gestionado por Linker_PSRAM_ext.ld)        │
 *   └──────────────────────────────────────────────┘
 *
 *   DTCM (128 KB — 0x20000000)
 *   ┌──────────────────────────────────────────────┐ 0x20000000
 *   │  RAM disk FatFS                              │   64 KB
 *   │  /cf/startup.scr, /cf/dc_motor.tbl           │
 *   ├──────────────────────────────────────────────┤ 0x20010000
 *   │  Libre para expansión                        │   64 KB
 *   └──────────────────────────────────────────────┘ 0x2001FFFF
 *
 *   SRAM D2 (32 KB — 0x30000000)
 *   ┌──────────────────────────────────────────────┐ 0x30000000
 *   │  RAM cFE: SB routing, TBL buffers, EVS logs  │   32 KB
 *   └──────────────────────────────────────────────┘ 0x30007FFF
 *
 *   SRAM D3 (16 KB — 0x38000000)
 *   ┌──────────────────────────────────────────────┐ 0x38000000
 *   │  Reset area (persiste warm reset)            │    4 KB
 *   ├──────────────────────────────────────────────┤ 0x38001000
 *   │  User reserved area                          │    8 KB
 *   ├──────────────────────────────────────────────┤ 0x38003000
 *   │  Libre                                       │    4 KB
 *   └──────────────────────────────────────────────┘ 0x38003FFF
 *
 * @target  STM32H730IBT6Q (PCB custom + NOR Macronix + PSRAM APMemory)
 * @rtos    FreeRTOS v10.x + OSAL
 * @date    2026-05-23
 */

#ifndef PSP_STM32H730_H
#define PSP_STM32H730_H

#include "osal/osal_freertos.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════
 * IDENTIFICADORES DE PLATAFORMA
 * ══════════════════════════════════════════════════════════════════ */

#define CFE_PSP_CPU_ID           2u
#define CFE_PSP_CPU_NAME         "STM32H730IBT6Q-PSRAM"
#define CFE_PSP_SPACECRAFT_ID    42u

/* ══════════════════════════════════════════════════════════════════
 * MAPA DE MEMORIA — DTCM (128 KB — 0x20000000)
 * RAM disk para FatFS (cFS startup.scr, tablas)
 * ══════════════════════════════════════════════════════════════════ */

#define PSP_RAMDISK_ADDR         0x20000000UL
#define PSP_RAMDISK_SIZE         (64u * 1024u)   /* 64 KB = 128 sectores FAT */

/* ══════════════════════════════════════════════════════════════════
 * MAPA DE MEMORIA — SRAM D2 (32 KB — 0x30000000)
 * RAM cFE: SB routing table, TBL buffers, EVS logs
 * ══════════════════════════════════════════════════════════════════ */

#define PSP_CFE_RAM_ADDR         0x30000000UL
#define PSP_CFE_RAM_SIZE         (32u * 1024u)

/* ══════════════════════════════════════════════════════════════════
 * MAPA DE MEMORIA — SRAM D3 (16 KB — 0x38000000)
 * Reset area + user reserved area (regiones pequeñas, aisladas)
 * ══════════════════════════════════════════════════════════════════ */

#define PSP_RESET_AREA_ADDR      0x38000000UL
#define PSP_RESET_AREA_SIZE      (4u * 1024u)

#define PSP_USER_AREA_ADDR       0x38001000UL
#define PSP_USER_AREA_SIZE       (8u * 1024u)

/* ══════════════════════════════════════════════════════════════════
 * HEAP FREERTOS (referencia — vive en AXI-SRAM, gestionado por linker)
 * ══════════════════════════════════════════════════════════════════ */

#define PSP_FREERTOS_HEAP_SIZE   (64u * 1024u)   /* configTOTAL_HEAP_SIZE */

/* ══════════════════════════════════════════════════════════════════
 * TIPOS DE RESET
 * ══════════════════════════════════════════════════════════════════ */

#define CFE_PSP_RST_TYPE_POWERON    1u
#define CFE_PSP_RST_TYPE_PROCESSOR  2u

/* ══════════════════════════════════════════════════════════════════
 * ESTRUCTURA DEL AREA DE RESET
 * ══════════════════════════════════════════════════════════════════ */

#define PSP_RESET_MAGIC   0xCAFE7300u  /* H730 */

typedef struct {
    uint32  magic;
    uint32  boot_type;
    uint32  reset_count;
    int32   last_error;
    char    last_app[32];
} PSP_ResetArea_t;

/* ══════════════════════════════════════════════════════════════════
 * TABLA DE APPS ESTATICAS
 * Vacia en Fase 1 — se llena cuando integremos dc_motor_app (Fase 6).
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    const char        *name;
    osal_task_entry_t  entry_point;
} PSP_AppEntry_t;

extern const PSP_AppEntry_t PSP_AppTable[];

/* ══════════════════════════════════════════════════════════════════
 * PROTOTIPOS PSP
 * ══════════════════════════════════════════════════════════════════ */

void        CFE_PSP_Main(void);

uint32      CFE_PSP_GetProcessorId(void);
uint32      CFE_PSP_GetSpacecraftId(void);
uint32      CFE_PSP_GetBootType(void);
const char *CFE_PSP_GetProcessorName(void);

int32       CFE_PSP_GetVolatileDiskMem(uint32 *addr, uint32 *size);
int32       CFE_PSP_GetResetArea(uint32 *addr, uint32 *size);
int32       CFE_PSP_GetUserReservedArea(uint32 *addr, uint32 *size);

void        CFE_PSP_GetTime(uint32 *seconds, uint32 *microseconds);
uint32      CFE_PSP_GetTimerTicksPerSecond(void);

void        CFE_PSP_Panic(int32 error_code);
void        CFE_PSP_Restart(uint32 reset_type);

int32       PSP_FS_Init(void);   /* stub en Fase 1, real en Fase 2 */

osal_task_entry_t CFE_PSP_FindAppEntry(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* PSP_STM32H730_H */
