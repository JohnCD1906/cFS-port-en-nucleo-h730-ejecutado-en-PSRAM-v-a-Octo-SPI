/**

- @file  cfe_default_files.h
- @brief Archivos de cFE hardcodeados en Flash como const arrays
- 
- En Linux, cFE lee estos archivos desde el disco en /cf/.
- En STM32 no hay disco en el arranque, por lo que los datos
- viven aquí como arrays const (van a la sección .rodata en Flash).
- 
- PSP_FS_Init() los copia al RAM disk al arrancar:
- 
- Flash (read-only)              RAM disk (FatFS)
- cfe_es_startup_scr[] ──────► /cf/cfe_es_startup.scr
- cfe_dc_motor_tbl[]   ──────► /cf/dc_motor.tbl
- 
- ── Formato de cfe_es_startup.scr ──────────────────────────────
- 
- Cada línea define una app que cFE ES debe arrancar.
- Formato:
- ```
  <Tipo>, <Path>, <EntryPoint>, <Nombre>, <Prioridad>, <Stack>, <ExcAction>;
  ```
- 
- Campos:
- ```
  Tipo        : CFE_APP (aplicación) o CFE_LIB (librería)
  ```
- ```
  Path        : En Linux = path al .so. En STM32 = ignorado
  ```
- ```
                (PSP busca el entry point en PSP_AppTable[])
  ```
- ```
  EntryPoint  : nombre del símbolo — debe coincidir con PSP_AppTable
  ```
- ```
  Nombre      : identificador de la app en cFE (max 20 chars)
  ```
- ```
  Prioridad   : 1-255, prioridad de la tarea principal
  ```
- ```
  Stack       : bytes de stack para la tarea principal
  ```
- ```
  ExcAction   : 0=restart processor, 1=restart app
  ```
- 
- ── Cómo añadir una nueva app ───────────────────────────────────
- 
- 1. Añadir una línea en cfe_es_startup_scr[] abajo
- 1. Añadir la entrada en PSP_AppTable[] (psp_stm32f407.c)
- 1. Incluir el header de la app en psp_stm32f407.c
- 
- ── Cómo actualizar en vuelo (futuro) ───────────────────────────
- 
- Cuando cFE TBL services esté implementado, las tablas .tbl
- podrán cargarse vía uplink (ground command) y reemplazar
- los valores del RAM disk sin necesidad de reflashear.
  */

#ifndef CFE_DEFAULT_FILES_H
#define CFE_DEFAULT_FILES_H

#include "osal_freertos.h"

#ifdef __cplusplus
extern "C"{
#endif

/* ══════════════════════════════════════════════════════════════════

- cfe_es_startup.scr
- 
- Script de arranque de cFE Executive Services.
- PSP_FS_Init() lo escribe en /cf/cfe_es_startup.scr del RAM disk.
- ══════════════════════════════════════════════════════════════════ */
  static const uint8 startup_scr[] =
  "; cFE ES Startup Script — STM32F439 Nucleo\n"
  "; Generado por PSP port v0.1 — 2026-03-12\n"
  ";\n"
  "; Formato:\n"
  ";   Tipo, Path, EntryPoint, NombreApp, Prioridad, Stack, ExcAction;\n"
  ";\n"
  "; NOTA STM32: Path es ignorado (linking estático).\n"
  ";   PSP busca EntryPoint en PSP_AppTable[].\n"
  ";\n"
  
  /* App de control de motor DC */
  "CFE_APP, /cf/dc_motor_app, DC_MOTOR_AppMain, DCMotor, 80, 4096, 0;\n"
  
  /* Aquí irán las apps de cFE cuando se porten: */
  /* “CFE_APP, /cf/ci_lab,      CI_Lab_AppMain,   CI_LAB,  70, 8192, 0;\n” */
  /* “CFE_APP, /cf/to_lab,      TO_Lab_AppMain,   TO_LAB,  70, 8192, 0;\n” */
  /* “CFE_APP, /cf/sch_lab,     SCH_Lab_AppMain,  SCH_LAB, 90, 8192, 0;\n” */
  ;

static const uint32 startup_scr_len =
(uint32)(sizeof(startup_scr) - 1u);  /* -1: sin el ‘\0’ final */

/* ══════════════════════════════════════════════════════════════════

- dc_motor.tbl — tabla de parámetros del motor DC
- 
- En cFE completo, TBL services carga esta tabla y permite
- actualizarla desde tierra vía uplink sin reiniciar.
- 
- Por ahora es un placeholder con los parámetros de la app.
- Formato: texto con clave=valor por línea (fácil de parsear).
- 
- Cuando se implemente TBL services, migrar a formato binario
- con header cFE estándar.
- ══════════════════════════════════════════════════════════════════ */
  static const uint8 cfe_dc_motor_tbl[] =
  "; dc_motor_app — tabla de parámetros v1.0\n"
  "; TBL services (futuro) cargará y validará este archivo\n"
  ";\n"
  "PWM_ARR=999\n"           /* DC_PWM_ARR: resolución del TIM1 */
  "RAMP_STEP_PCT=5\n"       /* DC_RAMP_STEP_PCT: paso de rampa */
  "RAMP_DELAY_MS=50\n"      /* DC_RAMP_DELAY_MS: delay entre pasos */
  "CMD_QUEUE_DEPTH=4\n"     /* DC_CMD_QUEUE_DEPTH */
  "PWM_QUEUE_DEPTH=2\n"     /* DC_PWM_QUEUE_DEPTH */
  "SEQ_TASK_PRIO=3\n"       /* DC_SEQ_TASK_PRIO (escala OSAL) */
  "CTRL_TASK_PRIO=2\n"      /* DC_CTRL_TASK_PRIO (escala OSAL) */
  "PWM_TASK_PRIO=1\n"       /* DC_PWM_TASK_PRIO (escala OSAL) */
  ;

static const uint32 cfe_dc_motor_tbl_len =
(uint32)(sizeof(cfe_dc_motor_tbl) - 1u);

#ifdef __cplusplus
}
#endif

#endif /* CFE_DEFAULT_FILES_H */
