/**

- @file  psp_stm32f439.h
- @brief PSP — Platform Support Package para STM32F439ZI Nucleo-144
- 
- ── Diferencias F439ZI vs F407VG ────────────────────────────────
- 
- F439ZI tiene 256 KB de SRAM (F407 tiene 192 KB):
- ```
  SRAM1 : 112 KB en 0x20000000  (acceso general + DMA)
  ```
- ```
  SRAM2 :  16 KB en 0x2001C000  (acceso general + DMA)
  ```
- ```
  SRAM3 :  64 KB en 0x20020000  (acceso general + DMA) ← EXTRA F439
  ```
- ```
  CCMRAM:  64 KB en 0x10000000  (solo CPU, sin DMA)
  ```
- 
- ── Mapa de memoria elegido ─────────────────────────────────────
-
- SRAM1 + SRAM2 (128 KB — 0x20000000..0x2001FFFF)
- ┌──────────────────────────────────────────────┐ 0x20000000
- │  Stack inicial + vectores (linker script)    │   ~8 KB
- ├──────────────────────────────────────────────┤ 0x20002000
- │  Heap FreeRTOS (64 KB)                       │   64 KB
- │  configTOTAL_HEAP_SIZE = 65536               │
- ├──────────────────────────────────────────────┤ 0x20012000
- │  Área de reset PSP (warm/cold start)         │    4 KB
- │  Persiste entre warm resets                  │
- ├──────────────────────────────────────────────┤ 0x20013000
- │  Área reservada para apps                    │    8 KB
- ├──────────────────────────────────────────────┤ 0x20015000
- │  Libre / buffers HAL                         │   ~44 KB
- └──────────────────────────────────────────────┘ 0x2001FFFF
- 
- SRAM3 (64 KB — 0x20020000..0x2002FFFF) ← exclusivo F439
- ┌──────────────────────────────────────────────┐ 0x20020000
- │  RAM disk para FatFS (48 KB)                 │   48 KB
- │  /cf/cfe_es_startup.scr                      │
- │  /cf/dc_motor.tbl                            │
- ├──────────────────────────────────────────────┤ 0x2002C000
- │  Libre para expansión                        │   16 KB
- └──────────────────────────────────────────────┘ 0x2002FFFF
- 
- CCMRAM (64 KB — 0x10000000..0x1000FFFF)
- ┌──────────────────────────────────────────────┐ 0x10000000
- │  RAM cFE — heap SB, TBL, EVS                 │   48 KB
- ├──────────────────────────────────────────────┤ 0x1000C000
- │  Libre para expansión cFE                    │   16 KB
- └──────────────────────────────────────────────┘ 0x1000FFFF
- 
- ── Ventaja de poner el RAM disk en SRAM3 ───────────────────────
- 
- SRAM3 es una región separada — no hay riesgo de que el heap
- de FreeRTOS o el stack crezcan y sobreescriban el filesystem.
- Además 48 KB de RAM disk dan ~46 KB útiles (vs 24 KB antes).
- 
- @target  STM32F439ZI Nucleo-144
- @rtos    FreeRTOS v10.x sobre OSAL
- @date    2026-03-21
  */

#ifndef PSP_STM32F439_H
#define PSP_STM32F439_H

#include "osal_freertos.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════

- IDENTIFICADORES DE PLATAFORMA
- ══════════════════════════════════════════════════════════════════ */

#define CFE_PSP_CPU_ID           1u
#define CFE_PSP_CPU_NAME         "STM32F439ZI-Nucleo"
#define CFE_PSP_SPACECRAFT_ID    42u

/* ══════════════════════════════════════════════════════════════════

- MAPA DE MEMORIA — SRAM1+2 (128 KB — 0x20000000)
- ══════════════════════════════════════════════════════════════════ */

/** Heap FreeRTOS — usar este valor en configTOTAL_HEAP_SIZE */
#define PSP_FREERTOS_HEAP_SIZE   (64u * 1024u)   /* 64 KB */

/** Area de reset — persiste entre warm resets */
#define PSP_RESET_AREA_ADDR      0x20012000UL
#define PSP_RESET_AREA_SIZE      (4u * 1024u)    /* 4 KB */

/** Area reservada para apps de usuario */
#define PSP_USER_AREA_ADDR       0x20013000UL
#define PSP_USER_AREA_SIZE       (8u * 1024u)    /* 8 KB */

/* ══════════════════════════════════════════════════════════════════

- MAPA DE MEMORIA — SRAM3 (64 KB — 0x20020000) ← exclusivo F439
-
- RAM disk aqui — separado del heap FreeRTOS, sin riesgo de colision
- ══════════════════════════════════════════════════════════════════ */

/** RAM disk para FatFS — 48 KB en SRAM3 */
#define PSP_RAMDISK_ADDR         0x20018000UL
#define PSP_RAMDISK_SIZE         (64u * 1024u)   /* 48 KB = 96 sectores FAT */

/* ══════════════════════════════════════════════════════════════════

- MAPA DE MEMORIA — CCMRAM (64 KB — 0x10000000, solo CPU)
- ══════════════════════════════════════════════════════════════════ */

/** RAM para cFE: SB routing table, TBL buffers, EVS logs */
#define PSP_CFE_RAM_ADDR         0x10000000UL
#define PSP_CFE_RAM_SIZE         (48u * 1024u)   /* 48 KB */

/* ══════════════════════════════════════════════════════════════════

- TIPOS DE RESET
- ══════════════════════════════════════════════════════════════════ */

#define CFE_PSP_RST_TYPE_POWERON    1u
#define CFE_PSP_RST_TYPE_PROCESSOR  2u

/* ══════════════════════════════════════════════════════════════════

- ESTRUCTURA DEL AREA DE RESET
- ══════════════════════════════════════════════════════════════════ */

#define PSP_RESET_MAGIC   0xCAFEF439u  /* F439 = STM32F439 */

typedef struct {
uint32  magic;
uint32  boot_type;
uint32  reset_count;
int32   last_error;
char    last_app[32];
} PSP_ResetArea_t;

/* ══════════════════════════════════════════════════════════════════

- TABLA DE APPS ESTATICAS
- ══════════════════════════════════════════════════════════════════ */

typedef struct {
const char        *name;
osal_task_entry_t  entry_point;
} PSP_AppEntry_t;

extern const PSP_AppEntry_t PSP_AppTable[];

/* ══════════════════════════════════════════════════════════════════

- PROTOTIPOS PSP
- ══════════════════════════════════════════════════════════════════ */

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

int32       PSP_FS_Init(void);
osal_task_entry_t CFE_PSP_FindAppEntry(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* PSP_STM32F439_H */
