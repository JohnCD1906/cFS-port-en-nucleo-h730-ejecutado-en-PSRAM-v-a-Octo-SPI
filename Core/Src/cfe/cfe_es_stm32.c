/**
 * @file  cfe_es_stm32.c
 * @brief cFE Executive Services — implementación mínima para STM32
 *
 * ── Origen del código ────────────────────────────────────────────
 *
 *   Adaptado de NASA cFS Draco (Apache 2.0):
 *     cfe/modules/es/fsw/src/cfe_es_start.c  → CFE_ES_Main, StartApplications, ParseFileEntry
 *     cfe/modules/es/fsw/src/cfe_es_api.c    → WriteToSysLog, RunLoop, GetAppID, ExitApp
 *
 *   Cambios respecto al original NASA:
 *     - Eliminado: OS_mkfs / OS_mount (ya hecho en PSP_FS_Init)
 *     - Eliminado: dlopen / OS_ModuleLoad → usa PSP_AppTable[]
 *     - Eliminado: CFE_ES_CreateObjects (core apps como tareas separadas)
 *     - Eliminado: CDS, background task, perf logging
 *     - Eliminado: sig_atomic_t → volatile uint32
 *     - Simplificado: Memory Pool con first-fit propio
 *     - Simplificado: AppRecord sin módulo OSAL ni load status
 *
 * @target  STM32F439ZI / STM32H730VBT6
 * @date    2026-03-30
 */

#include "cfe/cfe_es_stm32.h"
#include "cfe/cfe_evs_stm32.h"
#include "cfe/cfe_sb_stm32.h"
#include "osal/osal_freertos.h"
#include "osal/osal_freertos_fs.h"
#include "psp/psp_stm32h730.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>

/* ══════════════════════════════════════════════════════════════════
 * VARIABLE GLOBAL
 * ══════════════════════════════════════════════════════════════════ */

CFE_ES_Global_t CFE_ES_Global;

/* ══════════════════════════════════════════════════════════════════
 * HELPERS INTERNOS
 * ══════════════════════════════════════════════════════════════════ */

void CFE_ES_LockSharedData(void)
{
    OS_MutexLock(CFE_ES_Global.SharedDataMutex);
}

void CFE_ES_UnlockSharedData(void)
{
    OS_MutexUnlock(CFE_ES_Global.SharedDataMutex);
}

/*
 * Busca en AppTable la entrada cuyo MainTaskId coincide con el
 * TaskId actual. No requiere lock porque solo lee.
 */
static CFE_ES_AppRecord_t *CFE_ES_GetAppRecordByContext(void)
{
    osal_id_t     cur_task = OS_TaskGetId();
    uint32        i;

    for (i = 0; i < CFE_PLATFORM_ES_MAX_APPLICATIONS; i++)
    {
        if (CFE_ES_Global.AppTable[i].InUse &&
            CFE_ES_Global.AppTable[i].MainTaskId == cur_task)
        {
            return &CFE_ES_Global.AppTable[i];
        }
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_ES_WriteToSysLog
 *
 * En STM32 el "System Log" es simplemente el UART.
 * Adaptado de cfe_es_api.c — eliminado el buffer circular
 * del área de reset para simplificar.
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_ES_WriteToSysLog(const char *SpecStringPtr, ...)
{
    char    buf[128];
    va_list args;

    if (SpecStringPtr == NULL)
        return CFE_ES_BAD_ARGUMENT;

    va_start(args, SpecStringPtr);
    vsnprintf(buf, sizeof(buf), SpecStringPtr, args);
    va_end(args);

    OS_printf("%s", buf);

    return CFE_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_ES_RegisterApp
 *
 * Busca la entrada en AppTable por TaskId y la transiciona
 * a EARLY_INIT. Si no existe, significa que la app fue arrancada
 * fuera de CFE_ES_Main (situación de error).
 *
 * Lógica adaptada de cfe_es_api.c → no tiene equivalente directo
 * en el original porque allí el registro lo hace ES al cargar.
 * En STM32 la app lo hace explícitamente al inicio de su main.
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_ES_RegisterApp(void)
{
    CFE_ES_AppRecord_t *rec;

    CFE_ES_LockSharedData();
    rec = CFE_ES_GetAppRecordByContext();
    if (rec != NULL)
    {
        if (rec->AppState == CFE_ES_AppState_UNDEFINED)
            rec->AppState = CFE_ES_AppState_EARLY_INIT;
    }
    CFE_ES_UnlockSharedData();

    return (rec != NULL) ? CFE_SUCCESS : CFE_ES_ERR_APP_REGISTER;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_ES_GetAppID
 *
 * Adaptado de cfe_es_api.c CFE_ES_GetAppID().
 * La diferencia es que en el original busca en un TaskTable
 * separado; aquí buscamos directamente en AppTable por TaskId.
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_ES_GetAppID(CFE_ES_AppId_t *AppIdPtr)
{
    CFE_ES_AppRecord_t *rec;

    if (AppIdPtr == NULL)
        return CFE_ES_BAD_ARGUMENT;

    *AppIdPtr = CFE_ES_APPID_UNDEFINED;

    CFE_ES_LockSharedData();
    rec = CFE_ES_GetAppRecordByContext();
    if (rec != NULL)
        *AppIdPtr = rec->AppId;
    CFE_ES_UnlockSharedData();

    return (rec != NULL) ? CFE_SUCCESS : CFE_ES_ERR_RESOURCEID_NOT_VALID;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_ES_RunLoop
 *
 * Adaptado de cfe_es_api.c CFE_ES_RunLoop().
 * Diferencias:
 *   - No hay CFE_ES_IncrementTaskCounter (contador de ejecución)
 *   - La transición de estado a RUNNING es idéntica al original
 *   - ControlRequest es equivalente al AppControlRequest original
 * ══════════════════════════════════════════════════════════════════ */
bool CFE_ES_RunLoop(uint32 *RunStatus)
{
    CFE_ES_AppRecord_t *rec;
    bool                result;

    /* Si el status ya no es APP_RUN, retornar false directamente */
    if (RunStatus != NULL && *RunStatus != CFE_ES_RunStatus_APP_RUN)
        return false;

    CFE_ES_LockSharedData();

    rec = CFE_ES_GetAppRecordByContext();
    if (rec != NULL)
    {
        /* Transicionar a RUNNING si todavía no lo está */
        if (rec->AppState < CFE_ES_AppState_RUNNING)
            rec->AppState = CFE_ES_AppState_RUNNING;

        /* Retornar true solo si ES no pidió detener la app */
        if (rec->ControlRequest == CFE_ES_RunStatus_APP_RUN)
        {
            result = true;
        }
        else
        {
            if (RunStatus != NULL)
                *RunStatus = rec->ControlRequest;
            result = false;
        }
    }
    else
    {
        CFE_ES_WriteToSysLog("CFE_ES_RunLoop: no AppID para la tarea actual\n");
        result = false;
    }

    CFE_ES_UnlockSharedData();
    return result;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_ES_ExitApp
 *
 * Adaptado de cfe_es_api.c CFE_ES_ExitApp().
 * En el original la app external suspende en while(OS_TaskDelay)
 * y ES la elimina. En STM32 hacemos lo mismo porque no hay
 * scheduler de procesos — ES eventualmente llamará OS_TaskDelete.
 * ══════════════════════════════════════════════════════════════════ */
void CFE_ES_ExitApp(uint32 ExitStatus)
{
    CFE_ES_AppRecord_t *rec;

    CFE_ES_LockSharedData();
    rec = CFE_ES_GetAppRecordByContext();
    if (rec != NULL)
    {
        rec->AppState      = CFE_ES_AppState_STOPPED;
        rec->ControlRequest = ExitStatus;
        CFE_ES_WriteToSysLog("ES: App '%s' llamó ExitApp (status=%lu)\n",
                              rec->AppName, (unsigned long)ExitStatus);
    }
    CFE_ES_UnlockSharedData();

    /* Suspender la tarea hasta que ES la elimine */
    while (1)
    {
        OS_TaskDelay(500);
    }
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_ES_WaitForStartupSync
 *
 * Adaptado de cfe_es_api.c CFE_ES_WaitForStartupSync().
 * Espera a que CFE_ES_Global.SystemState sea OPERATIONAL.
 * ══════════════════════════════════════════════════════════════════ */
void CFE_ES_WaitForStartupSync(uint32 TimeOutMilliseconds)
{
    uint32 waited = 0;

    /* Marcar la app como LATE_INIT (lista para esperar) */
    CFE_ES_LockSharedData();
    {
        CFE_ES_AppRecord_t *rec = CFE_ES_GetAppRecordByContext();
        if (rec != NULL && rec->AppState < CFE_ES_AppState_LATE_INIT)
            rec->AppState = CFE_ES_AppState_LATE_INIT;
    }
    CFE_ES_UnlockSharedData();

    while (CFE_ES_Global.SystemState < CFE_ES_SystemState_OPERATIONAL)
    {
        if (waited >= TimeOutMilliseconds)
        {
            CFE_ES_WriteToSysLog("ES: WaitForStartupSync timeout (%lu ms)\n",
                                  (unsigned long)TimeOutMilliseconds);
            break;
        }
        OS_TaskDelay(CFE_PLATFORM_ES_STARTUP_SYNC_POLL_MSEC);
        waited += CFE_PLATFORM_ES_STARTUP_SYNC_POLL_MSEC;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_ES_GetResetType
 * ══════════════════════════════════════════════════════════════════ */
int32 CFE_ES_GetResetType(uint32 *ResetSubtypePtr)
{
    if (ResetSubtypePtr != NULL)
        *ResetSubtypePtr = 0u;  /* subtype no implementado */

    return (int32)CFE_PSP_GetBootType();
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_ES_ParseFileEntry
 *
 * Parsea una línea tokenizada del startup.scr y arranca la app.
 *
 * Adaptado DIRECTAMENTE de cfe_es_start.c CFE_ES_ParseFileEntry().
 * Cambios:
 *   - Eliminado: OS_ModuleLoad / dlopen → CFE_PSP_FindAppEntry()
 *   - Eliminado: CFE_ES_AppCreate con módulo OSAL
 *   - Simplificado: crea la tarea directamente con OS_TaskCreate
 *
 * Formato esperado (8 tokens):
 *   CFE_APP, /cf/path, EntryPoint, NombreApp, Prioridad, Stack, ExcAction;
 *   [0]      [1]       [2]         [3]         [4]        [5]    [6]
 * ══════════════════════════════════════════════════════════════════ */
int32 CFE_ES_ParseFileEntry(const char **TokenList, uint32 NumTokens)
{
    const char        *EntryType;
    const char        *EntryPoint;
    const char        *AppName;
    uint32             Priority;
    uint32             StackSize;
    uint32             ExcAction;
    osal_task_entry_t  AppEntry;
    osal_id_t          TaskId;
    int32              Status;
    uint32             slot;
    CFE_ES_AppRecord_t *rec;

    /* Validación mínima de tokens — igual que en el original NASA */
    if (NumTokens < 7u)
    {
        CFE_ES_WriteToSysLog("ES: ParseFileEntry: pocos tokens (%lu)\n",
                              (unsigned long)NumTokens);
        return CFE_ES_ERR_APP_CREATE;
    }

    EntryType  = TokenList[0];
    /* TokenList[1] es el path — ignorado en STM32 (no hay dlopen) */
    EntryPoint = TokenList[2];
    AppName    = TokenList[3];
    Priority   = (uint32)strtoul(TokenList[4], NULL, 10);
    StackSize  = (uint32)strtoul(TokenList[5], NULL, 10);
    ExcAction  = (uint32)strtoul(TokenList[6], NULL, 10);

    /* Solo soportamos CFE_APP por ahora (CFE_LIB ignorado) */
    if (strncmp(EntryType, "CFE_APP", 7) != 0)
    {
        CFE_ES_WriteToSysLog("ES: ParseFileEntry: tipo '%s' no soportado\n",
                              EntryType);
        return CFE_SUCCESS;  /* no es error, simplemente ignorar */
    }

    /* Valores por defecto si no se especifican */
    if (StackSize == 0u)
        StackSize = CFE_PLATFORM_ES_DEFAULT_STACK_SIZE;
    if (Priority == 0u)
        Priority = 80u;

    /* ── Buscar la función de entrada en PSP_AppTable ────────── */
    /* En Linux original aquí va OS_ModuleLoad() + OS_SymbolLookup()
     * En STM32 usamos la tabla estática del PSP */
    AppEntry = CFE_PSP_FindAppEntry(EntryPoint);
    if (AppEntry == NULL)
    {
        CFE_ES_WriteToSysLog(
            "ES: ParseFileEntry: EntryPoint '%s' no en PSP_AppTable\n",
            EntryPoint);
        return CFE_ES_ERR_APP_CREATE;
    }

    /* ── Encontrar slot libre en AppTable ──────────────────────── */
    CFE_ES_LockSharedData();

    slot = CFE_PLATFORM_ES_MAX_APPLICATIONS;
    for (uint32 i = 0; i < CFE_PLATFORM_ES_MAX_APPLICATIONS; i++)
    {
        if (!CFE_ES_Global.AppTable[i].InUse)
        {
            slot = i;
            break;
        }
    }

    if (slot == CFE_PLATFORM_ES_MAX_APPLICATIONS)
    {
        CFE_ES_UnlockSharedData();
        CFE_ES_WriteToSysLog("ES: ParseFileEntry: no hay slots de app libres\n");
        return CFE_ES_ERR_APP_CREATE;
    }

    /* ── Rellenar el registro ───────────────────────────────────── */
    rec = &CFE_ES_Global.AppTable[slot];
    memset(rec, 0, sizeof(*rec));

    rec->AppId          = (CFE_ES_AppId_t)slot;
    rec->InUse          = true;
    rec->AppState       = CFE_ES_AppState_EARLY_INIT;
    rec->ControlRequest = CFE_ES_RunStatus_APP_RUN;
    rec->Priority       = Priority;
    rec->StackSize      = StackSize;
    rec->ExceptionAction = ExcAction;

    strncpy(rec->AppName, AppName,
            sizeof(rec->AppName) - 1u);
    rec->AppName[sizeof(rec->AppName) - 1u] = '\0';

    /* MainTaskId se rellena después de OS_TaskCreate */
    CFE_ES_Global.RegisteredApps++;

    CFE_ES_UnlockSharedData();

    /* ── Crear la tarea OSAL ────────────────────────────────────── */
    /* En el original NASA esto lo hace CFE_ES_StartAppTask()
     * con un wrapper CFE_ES_TaskEntryPoint para sincronizar el
     * inicio. En STM32 lo llamamos directamente. */
    Status = OS_TaskCreate(&TaskId,
                            AppName,
                            AppEntry,
                            NULL,        /* stack: FreeRTOS lo aloca */
                            StackSize,
                            Priority,
                            0u);

    if (Status != OS_SUCCESS)
    {
        /* Rollback: limpiar el slot */
        CFE_ES_LockSharedData();
        memset(rec, 0, sizeof(*rec));
        CFE_ES_Global.RegisteredApps--;
        CFE_ES_UnlockSharedData();

        CFE_ES_WriteToSysLog(
            "ES: ParseFileEntry: OS_TaskCreate('%s') fallo (%ld)\n",
            AppName, (long)Status);
        return CFE_ES_ERR_APP_CREATE;
    }

    /* Guardar el TaskId en el registro */
    CFE_ES_LockSharedData();
    rec->MainTaskId = TaskId;
    CFE_ES_UnlockSharedData();

    CFE_ES_WriteToSysLog(
        "ES: App '%s' arrancada (slot=%lu, prio=%lu, stack=%lu)\n",
        AppName, (unsigned long)slot,
        (unsigned long)Priority, (unsigned long)StackSize);

    return CFE_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_ES_StartApplications
 *
 * Lee el startup.scr línea por línea y llama ParseFileEntry.
 *
 * Adaptado DIRECTAMENTE de cfe_es_start.c CFE_ES_StartApplications().
 * Cambios:
 *   - Usa OS_open / OS_read de nuestro OSAL (no POSIX)
 *   - Eliminado: volatile startup file check (no es necesario en STM32)
 *   - Mismo algoritmo de tokenizado
 * ══════════════════════════════════════════════════════════════════ */
void CFE_ES_StartApplications(uint32 ResetType, const char *StartFilePath)
{
    int32       fd;
    int32       bytes_read;
    char        line_buf[120];
    char        token_buf[120];
    uint32      line_len;
    char       *tokens[CFE_ES_STARTSCRIPT_MAX_TOKENS_PER_LINE];
    uint32      num_tokens;
    char       *ptr;
    char       *tok;
    int32       result;

    (void)ResetType;  /* no usado por ahora en STM32 */

    if (StartFilePath == NULL)
    {
        CFE_ES_WriteToSysLog("ES: StartApplications: path NULL\n");
        return;
    }

    CFE_ES_WriteToSysLog("ES: Leyendo startup script: %s\n", StartFilePath);

    fd = OS_open(StartFilePath, OS_READ_ONLY, 0);
    if (fd < 0)
    {
        CFE_ES_WriteToSysLog("ES: No se pudo abrir %s\n", StartFilePath);
        return;
    }

    /* ── Leer el archivo caracter a caracter para extraer líneas ── */
    /* Mismo enfoque que el original NASA — leer un byte a la vez
     * para no depender del tamaño del archivo */
    line_len = 0u;

    while (1)
    {
        char ch;
        bytes_read = OS_read(fd, &ch, 1);

        if (bytes_read <= 0)
        {
            /* EOF o error — procesar lo que queda en el buffer */
            if (line_len > 0u)
            {
                line_buf[line_len] = '\0';
                goto process_line;
            }
            break;
        }

        /* Acumular el caracter */
        if (ch == '\n' || ch == '\r')
        {
            /* Fin de línea — procesar */
            line_buf[line_len] = '\0';
            goto process_line;
        }
        else if (line_len < (sizeof(line_buf) - 1u))
        {
            line_buf[line_len++] = ch;
        }
        continue;

process_line:
        line_len = 0u;

        /* Ignorar comentarios y líneas vacías — igual que NASA */
        ptr = line_buf;
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        if (*ptr == ';' || *ptr == '#' || *ptr == '\0')
            continue;

        /* Tokenizar la línea por comas y punto y coma */
        strncpy(token_buf, ptr, sizeof(token_buf) - 1u);
        token_buf[sizeof(token_buf) - 1u] = '\0';

        num_tokens = 0u;
        tok = strtok(token_buf, ", ;");
        while (tok != NULL &&
               num_tokens < CFE_ES_STARTSCRIPT_MAX_TOKENS_PER_LINE)
        {
            /* Eliminar espacios al inicio/fin del token */
            while (*tok == ' ' || *tok == '\t') tok++;
            char *end = tok + strlen(tok) - 1;
            while (end > tok && (*end == ' ' || *end == '\t'))
                *end-- = '\0';

            tokens[num_tokens++] = tok;
            tok = strtok(NULL, ", ;");
        }

        if (num_tokens == 0u)
            continue;

        /* Parsear y arrancar la app */
        result = CFE_ES_ParseFileEntry(
            (const char **)tokens, num_tokens);

        if (result != CFE_SUCCESS)
        {
            CFE_ES_WriteToSysLog(
                "ES: Error arrancando app (linea: '%s') RC=%ld\n",
                ptr, (long)result);
        }
    }

    OS_close(fd);
    CFE_ES_WriteToSysLog("ES: StartApplications completo "
                          "(%lu apps registradas)\n",
                          (unsigned long)CFE_ES_Global.RegisteredApps);
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_ES_Main
 *
 * Punto de entrada de cFE ES.
 *
 * Adaptado de cfe_es_start.c CFE_ES_Main().
 * Cambios respecto al original:
 *   - Eliminado: OS_mkfs, OS_mount → ya hecho en PSP_FS_Init()
 *   - Eliminado: CFE_ES_CreateObjects (core apps EVS/SB/TBL/TIME)
 *   - Eliminado: CFE_ES_SetupPerfVariables
 *   - Simplificado: mutex creation con OS_MutexCreate de nuestro OSAL
 *   - PSP_AppTable[] en lugar de carga dinámica
 *
 * El PSP llama esta función en lugar de DC_MOTOR_AppMain():
 *   CFE_ES_Main(CFE_PSP_GetBootType(), 0, 0,
 *               CFE_PLATFORM_ES_NONVOL_STARTUP_FILE);
 * ══════════════════════════════════════════════════════════════════ */
void CFE_ES_Main(uint32 StartType, uint32 StartSubtype,
                 uint32 ModeId, const char *StartFilePath)
{
    int32 status;

    (void)StartSubtype;
    (void)ModeId;

    /* ── Limpiar estado global ─────────────────────────────────── */
    memset(&CFE_ES_Global, 0, sizeof(CFE_ES_Global));
    CFE_ES_Global.SystemState = CFE_ES_SystemState_EARLY_INIT;
    CFE_ES_Global.LastAppId   = 0u;
    CFE_ES_Global.LastPoolId  = 0u;

    /* ── Crear mutex de datos compartidos ─────────────────────── */
    /* En el original NASA se usa OS_MutSemCreate de OSAL completo.
     * En STM32 usamos OS_MutexCreate de nuestro OSAL/FreeRTOS. */
    status = OS_MutexCreate(&CFE_ES_Global.SharedDataMutex,
                             "ES_MUTEX", 0u);
    if (status != OS_SUCCESS)
    {
        OS_printf("ES FATAL: no se pudo crear mutex (%ld)\n",
                  (long)status);
        CFE_PSP_Panic(-1);
        return;
    }

    CFE_ES_WriteToSysLog("ES: CFE_ES_Main iniciando (StartType=%lu)\n",
                          (unsigned long)StartType);

    /* ── Transición a CORE_STARTUP ─────────────────────────────── */
    /* En el original aquí se crean las 5 core apps (EVS, SB, etc)
     * como tareas separadas con CFE_ES_CreateObjects().
     * En STM32 fase 1 saltamos ese paso — las core apps se
     * implementarán en fases siguientes. */
    CFE_ES_Global.SystemState = CFE_ES_SystemState_CORE_STARTUP;
    CFE_ES_WriteToSysLog("ES: estado CORE_STARTUP\n");

    /* ── Inicializar EVS ─────────────────────────────────────────── */
    if (CFE_EVS_EarlyInit() != CFE_SUCCESS)
        OS_printf("ES WARN: EVS init fallo — continuando sin EVS\n");

    /* ── Inicializar SB ──────────────────────────────────────────── */
    if (CFE_SB_EarlyInit() != CFE_SUCCESS)
        OS_printf("ES WARN: SB init fallo — mensajeria no disponible\n");

    /* ── Transición a CORE_READY ───────────────────────────────── */
    CFE_ES_Global.SystemState = CFE_ES_SystemState_CORE_READY;
    CFE_ES_WriteToSysLog("ES: estado CORE_READY\n");

    /* ── Arrancar las apps del startup.scr ─────────────────────── */
    /* Misma lógica que el original NASA:
     *   1. Leer y parsear el startup script
     *   2. Arrancar cada app como tarea OSAL
     *   3. Esperar a que lleguen al estado LATE_INIT */
    CFE_ES_StartApplications(StartType, StartFilePath);

    /* ── Esperar a que las apps inicialicen ────────────────────── */
    /* En el original NASA se usa CFE_ES_MainTaskSyncDelay().
     * En STM32 hacemos un polling simple del estado de las apps. */
    {
        uint32 waited = 0u;
        uint32 not_ready;
        uint32 i;

        while (waited < CFE_PLATFORM_ES_STARTUP_SCRIPT_TIMEOUT_MSEC)
        {
            not_ready = 0u;
            CFE_ES_LockSharedData();
            for (i = 0; i < CFE_PLATFORM_ES_MAX_APPLICATIONS; i++)
            {
                if (CFE_ES_Global.AppTable[i].InUse &&
                    CFE_ES_Global.AppTable[i].AppState
                        < CFE_ES_AppState_LATE_INIT)
                {
                    not_ready++;
                }
            }
            CFE_ES_UnlockSharedData();

            if (not_ready == 0u)
                break;

            OS_TaskDelay(CFE_PLATFORM_ES_STARTUP_SYNC_POLL_MSEC);
            waited += CFE_PLATFORM_ES_STARTUP_SYNC_POLL_MSEC;
        }

        if (waited >= CFE_PLATFORM_ES_STARTUP_SCRIPT_TIMEOUT_MSEC)
        {
            CFE_ES_WriteToSysLog(
                "ES: WARN: timeout esperando apps LATE_INIT\n");
        }
    }

    /* ── Transición a APPS_INIT → OPERATIONAL ──────────────────── */
    CFE_ES_Global.SystemState = CFE_ES_SystemState_APPS_INIT;
    CFE_ES_WriteToSysLog("ES: estado APPS_INIT\n");

    /* Esperar a que todas las apps estén RUNNING */
    {
        uint32 waited = 0u;
        uint32 not_running;
        uint32 i;

        while (waited < CFE_PLATFORM_ES_STARTUP_SCRIPT_TIMEOUT_MSEC)
        {
            not_running = 0u;
            CFE_ES_LockSharedData();
            for (i = 0; i < CFE_PLATFORM_ES_MAX_APPLICATIONS; i++)
            {
                if (CFE_ES_Global.AppTable[i].InUse &&
                    CFE_ES_Global.AppTable[i].AppState
                        < CFE_ES_AppState_RUNNING)
                {
                    not_running++;
                }
            }
            CFE_ES_UnlockSharedData();

            if (not_running == 0u)
                break;

            OS_TaskDelay(CFE_PLATFORM_ES_STARTUP_SYNC_POLL_MSEC);
            waited += CFE_PLATFORM_ES_STARTUP_SYNC_POLL_MSEC;
        }
    }

    CFE_ES_Global.SystemState = CFE_ES_SystemState_OPERATIONAL;
    CFE_ES_WriteToSysLog(
        "ES: estado OPERATIONAL — %lu app(s) activa(s)\n",
        (unsigned long)CFE_ES_Global.RegisteredApps);
}

/* ══════════════════════════════════════════════════════════════════
 * MEMORY POOL
 *
 * Implementación first-fit simple.
 * Cada bloque libre/allocado tiene un CFE_ES_PoolBlockHdr_t
 * al inicio. La lista de bloques es lineal — adyacentes en memoria.
 *
 * El SB usará esto para sus buffers de mensajes.
 * ══════════════════════════════════════════════════════════════════ */

/*
 * Crea un pool — implementación compartida para PoolCreate y PoolCreateNoSem
 */
static CFE_Status_t pool_create_impl(CFE_ES_MemHandle_t *PoolID,
                                      void *MemPtr, uint32 Size,
                                      bool UseMutex)
{
    CFE_ES_MemPoolRecord_t *prec;
    CFE_ES_PoolBlockHdr_t  *hdr;
    uint32                  slot;

    if (PoolID == NULL || MemPtr == NULL || Size == 0u)
        return CFE_ES_BAD_ARGUMENT;

    /* Necesitamos al menos un header + 4 bytes de datos */
    if (Size <= sizeof(CFE_ES_PoolBlockHdr_t))
        return CFE_ES_BAD_ARGUMENT;

    /* Buscar slot libre */
    slot = CFE_PLATFORM_ES_MAX_MEMORY_POOLS;
    for (uint32 i = 0; i < CFE_PLATFORM_ES_MAX_MEMORY_POOLS; i++)
    {
        if (!CFE_ES_Global.MemPoolTable[i].InUse)
        {
            slot = i;
            break;
        }
    }

    if (slot == CFE_PLATFORM_ES_MAX_MEMORY_POOLS)
    {
        CFE_ES_WriteToSysLog("ES: PoolCreate: sin slots disponibles\n");
        return CFE_ES_ERR_APP_REGISTER;
    }

    prec = &CFE_ES_Global.MemPoolTable[slot];
    memset(prec, 0, sizeof(*prec));

    prec->Handle    = (CFE_ES_MemHandle_t)slot;
    prec->BasePtr   = (uint8 *)MemPtr;
    prec->TotalSize = Size;
    prec->UseMutex  = UseMutex;
    prec->InUse     = true;

    if (UseMutex)
    {
        char mname[24];
        snprintf(mname, sizeof(mname), "POOL_MTX_%lu", (unsigned long)slot);
        OS_MutexCreate(&prec->Mutex, mname, 0u);
    }

    /* Inicializar el primer bloque (todo el espacio libre) */
    hdr        = (CFE_ES_PoolBlockHdr_t *)MemPtr;
    hdr->Magic = CFE_ES_POOL_BLOCK_MAGIC;
    hdr->Size  = Size;
    hdr->InUse = false;

    *PoolID = (CFE_ES_MemHandle_t)slot;

    CFE_ES_WriteToSysLog("ES: Pool creado (slot=%lu, @%p, %lu bytes)\n",
                          (unsigned long)slot, MemPtr,
                          (unsigned long)Size);
    return CFE_SUCCESS;
}

CFE_Status_t CFE_ES_PoolCreate(CFE_ES_MemHandle_t *PoolID,
                                void *MemPtr, uint32 Size)
{
    return pool_create_impl(PoolID, MemPtr, Size, true);
}

CFE_Status_t CFE_ES_PoolCreateNoSem(CFE_ES_MemHandle_t *PoolID,
                                     void *MemPtr, uint32 Size)
{
    return pool_create_impl(PoolID, MemPtr, Size, false);
}

int32 CFE_ES_GetPoolBuf(CFE_ES_MemPoolBuf_t *BufPtr,
                         CFE_ES_MemHandle_t Handle, uint32 Size)
{
    CFE_ES_MemPoolRecord_t *prec;
    CFE_ES_PoolBlockHdr_t  *hdr;
    uint8                  *cur;
    uint8                  *end;
    uint32                  needed;

    if (BufPtr == NULL || Handle >= CFE_PLATFORM_ES_MAX_MEMORY_POOLS)
        return CFE_ES_BAD_ARGUMENT;

    prec = &CFE_ES_Global.MemPoolTable[Handle];
    if (!prec->InUse)
        return CFE_ES_ERR_RESOURCEID_NOT_VALID;

    if (Size == 0u)
        return CFE_ES_ERR_MEM_BLOCK_SIZE;

    /* Alinear el tamaño a 4 bytes */
    needed = (Size + 3u) & ~3u;
    needed += (uint32)sizeof(CFE_ES_PoolBlockHdr_t);

    if (prec->UseMutex)
        OS_MutexLock(prec->Mutex);

    /* ── Búsqueda first-fit ──────────────────────────────────── */
    cur = prec->BasePtr;
    end = prec->BasePtr + prec->TotalSize;

    while (cur + sizeof(CFE_ES_PoolBlockHdr_t) <= end)
    {
        hdr = (CFE_ES_PoolBlockHdr_t *)cur;

        /* Verificar integridad del header */
        if (hdr->Magic != CFE_ES_POOL_BLOCK_MAGIC)
        {
            /* Corrupción del pool */
            if (prec->UseMutex) OS_MutexUnlock(prec->Mutex);
            return CFE_ES_BUFFER_NOT_IN_POOL;
        }

        if (!hdr->InUse && hdr->Size >= needed)
        {
            /* ── Encontrado — dividir si sobra espacio ─────── */
            if (hdr->Size >= needed + sizeof(CFE_ES_PoolBlockHdr_t) + 4u)
            {
                /* Crear bloque libre con el resto */
                CFE_ES_PoolBlockHdr_t *next =
                    (CFE_ES_PoolBlockHdr_t *)(cur + needed);
                next->Magic = CFE_ES_POOL_BLOCK_MAGIC;
                next->Size  = hdr->Size - needed;
                next->InUse = false;
                hdr->Size   = needed;
            }

            hdr->InUse = true;
            *BufPtr    = (void *)(cur + sizeof(CFE_ES_PoolBlockHdr_t));

            if (prec->UseMutex) OS_MutexUnlock(prec->Mutex);
            return (int32)(hdr->Size - sizeof(CFE_ES_PoolBlockHdr_t));
        }

        /* Avanzar al siguiente bloque */
        cur += hdr->Size;
        if (cur == end) break;  /* fin del pool */
    }

    if (prec->UseMutex) OS_MutexUnlock(prec->Mutex);

    CFE_ES_WriteToSysLog("ES: GetPoolBuf: sin memoria libre (%lu bytes)\n",
                          (unsigned long)Size);
    return CFE_ES_ERR_MEM_BLOCK_SIZE;
}

int32 CFE_ES_PutPoolBuf(CFE_ES_MemHandle_t Handle,
                         CFE_ES_MemPoolBuf_t BufPtr)
{
    CFE_ES_MemPoolRecord_t *prec;
    CFE_ES_PoolBlockHdr_t  *hdr;
    uint8                  *ptr;
    int32                   freed;

    if (BufPtr == NULL || Handle >= CFE_PLATFORM_ES_MAX_MEMORY_POOLS)
        return CFE_ES_BAD_ARGUMENT;

    prec = &CFE_ES_Global.MemPoolTable[Handle];
    if (!prec->InUse)
        return CFE_ES_ERR_RESOURCEID_NOT_VALID;

    ptr = (uint8 *)BufPtr - sizeof(CFE_ES_PoolBlockHdr_t);
    hdr = (CFE_ES_PoolBlockHdr_t *)ptr;

    /* Validar que el puntero está dentro del pool y tiene magic */
    if (ptr < prec->BasePtr ||
        ptr >= prec->BasePtr + prec->TotalSize ||
        hdr->Magic != CFE_ES_POOL_BLOCK_MAGIC)
    {
        return CFE_ES_BUFFER_NOT_IN_POOL;
    }

    if (prec->UseMutex)
        OS_MutexLock(prec->Mutex);

    freed = (int32)(hdr->Size - sizeof(CFE_ES_PoolBlockHdr_t));
    hdr->InUse = false;

    /* TODO: coalescing de bloques adyacentes libres (fase siguiente) */

    if (prec->UseMutex)
        OS_MutexUnlock(prec->Mutex);

    return freed;
}
