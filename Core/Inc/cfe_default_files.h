/**
 * @file  cfe_default_files.h
 * @brief Archivos de cFE hardcodeados en Flash como const arrays
 *
 * En Linux, cFE lee estos archivos desde el disco en /cf/.
 * En STM32 no hay disco al arranque, por lo que los datos
 * viven aqui como arrays const (van a la seccion .rodata).
 *
 * PSP_FS_Init() los copia al RAM disk al arrancar:
 *
 *   .rodata (PSRAM)           RAM disk (FatFS en DTCM)
 *   startup_scr[]      ────►  /cf/startup.scr
 *   cfe_dc_motor_tbl[] ────►  /cf/dc_motor.tbl
 *
 * Formato de startup.scr — cada linea define una app:
 *   Tipo, Path, EntryPoint, Nombre, Prioridad, Stack, ExcAction;
 *
 *   Tipo       : CFE_APP o CFE_LIB
 *   Path       : ignorado en STM32 (sin dlopen)
 *   EntryPoint : nombre del simbolo, debe estar en PSP_AppTable[]
 *   Nombre     : identificador en cFE (max 20 chars)
 *   Prioridad  : 1-255
 *   Stack      : bytes
 *   ExcAction  : 0=restart processor, 1=restart app
 *
 * NOTA Fase 2: el startup.scr referencia DC_MOTOR_AppMain pero
 * PSP_AppTable[] esta vacia. En Fase 2 solo VALIDAMOS que el
 * archivo se escribe correctamente al RAM disk. En Fase 3 cFE ES
 * intentara arrancarlo y fallara con "EntryPoint no en PSP_AppTable"
 * hasta que en Fase 6 integremos dc_motor_app.
 */
#ifndef CFE_DEFAULT_FILES_H
#define CFE_DEFAULT_FILES_H

#include "osal/osal_freertos.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═════════════════════════════════════════════════════════════════
 * startup.scr — script de arranque de cFE ES
 * ═════════════════════════════════════════════════════════════════ */
static const uint8 startup_scr[] =
    "; cFE ES Startup Script - STM32H730IBT6Q\n"
    "; Generado por PSP port H730 v0.1\n"
    ";\n"
    "; Formato:\n"
    ";   Tipo, Path, EntryPoint, NombreApp, Prioridad, Stack, ExcAction;\n"
    ";\n"
    "; NOTA: Path es ignorado (sin dlopen).\n"
    ";       PSP busca EntryPoint en PSP_AppTable[].\n"
    ";\n"
    "CFE_APP, /cf/dc_motor_app, DC_MOTOR_AppMain, DCMotor, 80, 4096, 0;\n"
    ;

static const uint32 startup_scr_len =
    (uint32)(sizeof(startup_scr) - 1u);  /* -1: sin el '\0' final */

/* ═════════════════════════════════════════════════════════════════
 * dc_motor.tbl — tabla de parametros del motor DC
 * (placeholder hasta que se implemente TBL services)
 * ═════════════════════════════════════════════════════════════════ */
static const uint8 cfe_dc_motor_tbl[] =
    "; dc_motor_app - tabla de parametros v1.0\n"
    ";\n"
    "PWM_ARR=999\n"
    "RAMP_STEP_PCT=5\n"
    "RAMP_DELAY_MS=50\n"
    "CMD_QUEUE_DEPTH=4\n"
    "PWM_QUEUE_DEPTH=2\n"
    "SEQ_TASK_PRIO=3\n"
    "CTRL_TASK_PRIO=2\n"
    "PWM_TASK_PRIO=1\n"
    ;

static const uint32 cfe_dc_motor_tbl_len =
    (uint32)(sizeof(cfe_dc_motor_tbl) - 1u);

#ifdef __cplusplus
}
#endif

#endif /* CFE_DEFAULT_FILES_H */
