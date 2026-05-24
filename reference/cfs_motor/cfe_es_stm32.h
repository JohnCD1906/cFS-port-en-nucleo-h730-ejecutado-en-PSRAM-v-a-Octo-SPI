/**
 * @file  cfe_es_stm32.h
 * @brief cFE Executive Services — implementación mínima para STM32
 *
 * ── Qué implementa este módulo ──────────────────────────────────
 *
 *   Esta es la implementación mínima de cFE ES necesaria para que
 *   las aplicaciones cFS puedan arrancar y comunicarse en un STM32
 *   sin MMU, sin dlopen() y sin sistema operativo POSIX.
 *
 *   Funciones implementadas (las que necesita dc_motor_app y el SB):
 *     CFE_ES_Main()            - Lee startup.scr y arranca apps
 *     CFE_ES_WriteToSysLog()   - Log via OS_printf
 *     CFE_ES_RegisterApp()     - Registra app en la tabla interna
 *     CFE_ES_GetAppID()        - Retorna AppID de la tarea actual
 *     CFE_ES_RunLoop()         - Control de ciclo de vida de la app
 *     CFE_ES_ExitApp()         - La app señala que termina
 *     CFE_ES_WaitForStartupSync() - Espera a que el sistema esté listo
 *     CFE_ES_GetResetType()    - Retorna tipo de reset (cold/warm)
 *
 *   Memory Pool (prerequisito del SB):
 *     CFE_ES_PoolCreate()      - Crea pool sobre PSP_CFE_RAM
 *     CFE_ES_GetPoolBuf()      - Alloca bloque del pool
 *     CFE_ES_PutPoolBuf()      - Libera bloque al pool
 *
 * ── Qué NO implementa (diferencias respecto a NASA cFE) ─────────
 *
 *   - dlopen() / carga dinámica → usa PSP_AppTable[] estático
 *   - OS_mkfs / OS_mount → ya hecho en PSP_FS_Init()
 *   - Critical Data Store (CDS) → no hay NVM dedicada en STM32
 *   - Background task → no es necesario en fase mínima
 *   - Perf logging → no necesario en fase mínima
 *   - SB command pipe de ES → no hay SB todavía
 *   - Core apps (EVS, SB, TBL, TIME como tareas) → fase siguiente
 *
 * ── Relación con el código NASA ─────────────────────────────────
 *
 *   La lógica de parseo del startup.scr proviene de:
 *     cfe/modules/es/fsw/src/cfe_es_start.c → CFE_ES_ParseFileEntry
 *   La lógica del RunLoop y GetAppID proviene de:
 *     cfe/modules/es/fsw/src/cfe_es_api.c
 *   Los tipos de datos son compatibles con:
 *     cfe/modules/es/fsw/inc/cfe_es_api_typedefs.h
 *
 * @target  STM32F439ZI / STM32H730VBT6
 * @rtos    FreeRTOS v10.x + OSAL v7 (port STM32)
 * @date    2026-03-30
 */

#ifndef CFE_ES_STM32_H
#define CFE_ES_STM32_H

#include "osal_freertos.h"
#include "psp_stm32f439.h"
#include "cfe_es_platform_cfg.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════
 * TIPOS BÁSICOS
 * ══════════════════════════════════════════════════════════════════ */

/** ID de aplicación cFE — índice en la AppTable */
typedef uint32  CFE_ES_AppId_t;

/** Tipo de retorno estándar cFE */
typedef int32   CFE_Status_t;

/** Handle de memory pool */
typedef uint32  CFE_ES_MemHandle_t;

/** Puntero a buffer del pool — void* para ser genérico */
typedef void *  CFE_ES_MemPoolBuf_t;

/* ══════════════════════════════════════════════════════════════════
 * CONSTANTES
 * ══════════════════════════════════════════════════════════════════ */

#define CFE_SUCCESS              ( 0)
#define CFE_ES_BAD_ARGUMENT      (-1)
#define CFE_ES_ERR_APP_CREATE    (-2)
#define CFE_ES_ERR_APP_REGISTER  (-3)
#define CFE_ES_ERR_RESOURCEID_NOT_VALID (-4)
#define CFE_ES_NOT_IMPLEMENTED   (-5)
#define CFE_ES_ERR_MEM_BLOCK_SIZE  (-6)
#define CFE_ES_BUFFER_NOT_IN_POOL  (-7)
#define CFE_ES_OPERATION_TIMED_OUT (-8)

/* Códigos EVS */
#define CFE_EVS_APP_NOT_REGISTERED       (-20)
#define CFE_STATUS_EXTERNAL_RESOURCE_FAIL (-21)

/** AppID inválido / no definido */
#define CFE_ES_APPID_UNDEFINED   ((CFE_ES_AppId_t)0xFFFFFFFFu)

/** Pool handle inválido */
#define CFE_ES_MEMHANDLE_UNDEFINED  ((CFE_ES_MemHandle_t)0xFFFFFFFFu)

/* ══════════════════════════════════════════════════════════════════
 * ESTADOS DEL SISTEMA
 * Mapea cFE SystemState para el handshake de startup
 * ══════════════════════════════════════════════════════════════════ */

#define CFE_ES_SystemState_EARLY_INIT    0u
#define CFE_ES_SystemState_CORE_STARTUP  1u
#define CFE_ES_SystemState_CORE_READY    2u
#define CFE_ES_SystemState_APPS_INIT     3u
#define CFE_ES_SystemState_OPERATIONAL   4u
#define CFE_ES_SystemState_SHUTDOWN      5u

/* ══════════════════════════════════════════════════════════════════
 * ESTADOS DE LA APP
 * ══════════════════════════════════════════════════════════════════ */

typedef uint32 CFE_ES_AppState_Enum_t;

#define CFE_ES_AppState_UNDEFINED    0u
#define CFE_ES_AppState_EARLY_INIT   1u
#define CFE_ES_AppState_LATE_INIT    2u
#define CFE_ES_AppState_RUNNING      3u
#define CFE_ES_AppState_WAITING      4u
#define CFE_ES_AppState_STOPPED      5u

/* ══════════════════════════════════════════════════════════════════
 * RUN STATUS — lo que la app le dice a ES
 * ══════════════════════════════════════════════════════════════════ */

#define CFE_ES_RunStatus_APP_RUN              1u
#define CFE_ES_RunStatus_APP_EXIT             2u
#define CFE_ES_RunStatus_APP_ERROR            3u
#define CFE_ES_RunStatus_SYS_DELETE           4u
#define CFE_ES_RunStatus_SYS_RESTART          5u
#define CFE_ES_RunStatus_CORE_APP_INIT_ERROR  6u
#define CFE_ES_RunStatus_CORE_APP_RUNTIME_ERROR 7u

/* ══════════════════════════════════════════════════════════════════
 * EXCEPCIÓN ACTION — qué hace ES si la app falla
 * ══════════════════════════════════════════════════════════════════ */

#define CFE_ES_ExceptionAction_PROC_RESTART  0u  /* reset del procesador */
#define CFE_ES_ExceptionAction_RESTART_APP   1u  /* sólo reiniciar la app */

/* ══════════════════════════════════════════════════════════════════
 * MUTEX PARA POOL
 * ══════════════════════════════════════════════════════════════════ */

#define CFE_ES_USE_MUTEX   1u
#define CFE_ES_NO_MUTEX    0u

/* ══════════════════════════════════════════════════════════════════
 * REGISTRO DE APPS
 * Equivalente simplificado de CFE_ES_AppRecord_t
 * ══════════════════════════════════════════════════════════════════ */

typedef struct
{
    CFE_ES_AppId_t         AppId;
    char                   AppName[OS_MAX_NAME_LEN];
    CFE_ES_AppState_Enum_t AppState;
    uint32                 ControlRequest;     /* RunStatus actual */
    osal_id_t              MainTaskId;         /* Task OSAL de la app */
    uint32                 Priority;
    uint32                 StackSize;
    uint32                 ExceptionAction;
    bool                   InUse;
} CFE_ES_AppRecord_t;

/* ══════════════════════════════════════════════════════════════════
 * MEMORY POOL RECORD
 * Bloque de pool de memoria simple — first-fit con header
 * ══════════════════════════════════════════════════════════════════ */

/**
 * Header de cada bloque del pool.
 * Vive en los primeros bytes de cada bloque allocado.
 */
typedef struct
{
    uint32  Magic;      /* 0xB10CB10C si válido */
    uint32  Size;       /* tamaño del bloque completo (header + datos) */
    bool    InUse;      /* true = allocado, false = libre */
} CFE_ES_PoolBlockHdr_t;

#define CFE_ES_POOL_BLOCK_MAGIC  0xB10CB10Cu

typedef struct
{
    CFE_ES_MemHandle_t  Handle;
    uint8              *BasePtr;     /* puntero al inicio de la RAM */
    uint32              TotalSize;   /* bytes totales del pool */
    osal_id_t           Mutex;       /* mutex si USE_MUTEX */
    bool                UseMutex;
    bool                InUse;
} CFE_ES_MemPoolRecord_t;

/* ══════════════════════════════════════════════════════════════════
 * ESTADO GLOBAL DE ES
 * Versión mínima de CFE_ES_Global_t para STM32
 * ══════════════════════════════════════════════════════════════════ */

typedef struct
{
    /** Estado del sistema (CFE_ES_SystemState_*) */
    volatile uint32     SystemState;

    /** Mutex para proteger la tabla de apps */
    osal_id_t           SharedDataMutex;

    /** Tabla de apps registradas */
    CFE_ES_AppRecord_t  AppTable[CFE_PLATFORM_ES_MAX_APPLICATIONS];
    uint32              RegisteredApps;

    /** Último AppId asignado */
    uint32              LastAppId;

    /** Tabla de memory pools */
    CFE_ES_MemPoolRecord_t MemPoolTable[CFE_PLATFORM_ES_MAX_MEMORY_POOLS];
    uint32              LastPoolId;

} CFE_ES_Global_t;

/** Variable global única de ES */
extern CFE_ES_Global_t CFE_ES_Global;

/* ══════════════════════════════════════════════════════════════════
 * API PÚBLICA — compatible con cFE oficial
 * ══════════════════════════════════════════════════════════════════ */

/**
 * @brief Punto de entrada principal de cFE ES
 *
 * Lee /cf/cfe_es_startup.scr y arranca cada app listada.
 * Reemplaza la llamada directa a DC_MOTOR_AppMain() en el PSP.
 *
 * Adaptado de CFE_ES_Main() en cfe_es_start.c:
 *   - Eliminado: OS_mkfs/mount (ya hecho en PSP_FS_Init)
 *   - Eliminado: CFE_ES_CreateObjects (core apps como tareas)
 *   - Adaptado:  PSP_AppTable[] en lugar de dlopen()
 *
 * @param StartType     CFE_PSP_RST_TYPE_POWERON o PROCESSOR
 * @param StartSubtype  Subtipo de reset (no usado en STM32)
 * @param ModeId        Fuente de boot (no usado en STM32)
 * @param StartFilePath Path al startup script (ej. "/cf/cfe_es_startup.scr")
 */
void CFE_ES_Main(uint32 StartType, uint32 StartSubtype,
                 uint32 ModeId, const char *StartFilePath);

/**
 * @brief Escribe un mensaje en el System Log
 *
 * En STM32 redirige a OS_printf (UART3).
 * Compatible con cFE: acepta formato printf.
 *
 * @param SpecStringPtr  Formato printf
 * @return CFE_SUCCESS
 */
CFE_Status_t CFE_ES_WriteToSysLog(const char *SpecStringPtr, ...);

/**
 * @brief Registra la app actual en la tabla de ES
 *
 * Debe ser llamada por cada app al inicio de su main task.
 * Busca la entrada por TaskId y la marca como RUNNING.
 *
 * @return CFE_SUCCESS o CFE_ES_ERR_APP_REGISTER
 */
CFE_Status_t CFE_ES_RegisterApp(void);

/**
 * @brief Obtiene el AppID de la tarea que llama
 *
 * Busca en AppTable la entrada cuyo MainTaskId coincide
 * con el TaskId actual de OSAL.
 *
 * @param[out] AppIdPtr  Puntero donde guardar el AppID
 * @return CFE_SUCCESS o CFE_ES_ERR_RESOURCEID_NOT_VALID
 */
CFE_Status_t CFE_ES_GetAppID(CFE_ES_AppId_t *AppIdPtr);

/**
 * @brief Control del ciclo de vida de la app
 *
 * La app llama esta función al inicio de cada iteración
 * de su bucle principal. Retorna true si debe continuar,
 * false si ES quiere que la app termine (delete, restart, etc.)
 *
 * También actualiza el estado de la app a RUNNING.
 *
 * @param[in,out] RunStatus  Puntero al estado de ejecución (puede ser NULL)
 * @return true  → continuar ejecutando
 * @return false → terminar (ES lo solicita o RunStatus != APP_RUN)
 */
bool CFE_ES_RunLoop(uint32 *RunStatus);

/**
 * @brief La app señala que va a terminar
 *
 * Actualiza el estado en AppTable y suspende la tarea
 * esperando que ES la limpie (en STM32 hace OS_TaskDelay
 * indefinido hasta que ES la elimine).
 *
 * @param ExitStatus  CFE_ES_RunStatus_APP_EXIT o APP_ERROR
 */
void CFE_ES_ExitApp(uint32 ExitStatus);

/**
 * @brief Espera a que el sistema alcance el estado OPERATIONAL
 *
 * Llamada al final de la inicialización de una app para
 * esperar a que todas las demás apps hayan iniciado.
 *
 * @param TimeOutMilliseconds  Timeout en ms
 */
void CFE_ES_WaitForStartupSync(uint32 TimeOutMilliseconds);

/**
 * @brief Retorna el tipo de reset actual
 *
 * @param[out] ResetSubtypePtr  Subtipo (puede ser NULL)
 * @return CFE_PSP_RST_TYPE_POWERON o CFE_PSP_RST_TYPE_PROCESSOR
 */
int32 CFE_ES_GetResetType(uint32 *ResetSubtypePtr);

/* ══════════════════════════════════════════════════════════════════
 * MEMORY POOL API
 * Prerequisito para cFE SB
 * ══════════════════════════════════════════════════════════════════ */

/**
 * @brief Crea un pool de memoria
 *
 * Inicializa una región de RAM como pool de bloques variables.
 * Internamente usa un algoritmo first-fit con headers.
 *
 * Para el SB usar: CFE_ES_PoolCreate(&SB_PoolId, (void*)PSP_CFE_RAM_ADDR, PSP_CFE_RAM_SIZE)
 *
 * @param[out] PoolID   Handle del pool creado
 * @param[in]  MemPtr   Puntero al bloque de RAM
 * @param[in]  Size     Tamaño en bytes
 * @return CFE_SUCCESS o error
 */
CFE_Status_t CFE_ES_PoolCreate(CFE_ES_MemHandle_t *PoolID,
                                void *MemPtr, uint32 Size);

/**
 * @brief Crea pool sin mutex (para uso en contexto de ISR/startup)
 *
 * @param[out] PoolID   Handle del pool
 * @param[in]  MemPtr   Bloque de RAM
 * @param[in]  Size     Tamaño en bytes
 * @return CFE_SUCCESS o error
 */
CFE_Status_t CFE_ES_PoolCreateNoSem(CFE_ES_MemHandle_t *PoolID,
                                     void *MemPtr, uint32 Size);

/**
 * @brief Obtiene un buffer del pool
 *
 * Busca el primer bloque libre que sea >= Size bytes.
 *
 * @param[out] BufPtr  Puntero al puntero del buffer allocado
 * @param[in]  Handle  Handle del pool
 * @param[in]  Size    Tamaño solicitado en bytes
 * @return Bytes allocados (>= Size) o código de error negativo
 */
int32 CFE_ES_GetPoolBuf(CFE_ES_MemPoolBuf_t *BufPtr,
                         CFE_ES_MemHandle_t Handle, uint32 Size);

/**
 * @brief Libera un buffer al pool
 *
 * @param[in]  Handle  Handle del pool
 * @param[in]  BufPtr  Buffer a liberar
 * @return Bytes liberados o código de error negativo
 */
int32 CFE_ES_PutPoolBuf(CFE_ES_MemHandle_t Handle,
                         CFE_ES_MemPoolBuf_t BufPtr);

/* ══════════════════════════════════════════════════════════════════
 * FUNCIONES INTERNAS — no llamar directamente desde apps
 * ══════════════════════════════════════════════════════════════════ */

/** Parsea una línea del startup.scr y arranca la app */
int32 CFE_ES_ParseFileEntry(const char **TokenList, uint32 NumTokens);

/** Lee el startup.scr y llama CFE_ES_ParseFileEntry por cada línea */
void CFE_ES_StartApplications(uint32 ResetType, const char *StartFilePath);

/** Toma el mutex de datos compartidos de ES */
void CFE_ES_LockSharedData(void);

/** Libera el mutex de datos compartidos de ES */
void CFE_ES_UnlockSharedData(void);

#ifdef __cplusplus
}
#endif

#endif /* CFE_ES_STM32_H */
