/**
 * @file  cfe_evs_stm32.c
 * @brief cFE Event Services — implementación mínima para STM32
 *
 * ── Origen del código ────────────────────────────────────────────
 *
 *   Adaptado de NASA cFS Draco (Apache 2.0):
 *     cfe/modules/evs/fsw/src/cfe_evs_task.c  → init, Register
 *     cfe/modules/evs/fsw/src/cfe_evs_log.c   → SendEvent
 *
 *   Cambios respecto al original NASA:
 *     - Eliminado: telemetry output (requiere SB)
 *     - Eliminado: log buffer circular en área de reset
 *     - Eliminado: CFE_TBL, CFE_SB dependencies
 *     - Simplificado: filtrado binario sin semáforos de telemetría
 *     - Salida: OS_printf con prefijo [EVS/AppName/TIPO]
 *
 * ── Dependencias ─────────────────────────────────────────────────
 *
 *   - osal_freertos.h  → OS_printf, OS_MutexCreate/Lock/Unlock
 *   - cfe_es_stm32.h   → CFE_ES_GetAppID, CFE_ES_Global
 *
 * @target  STM32F439ZI / STM32H730VBT6
 * @date    2026-04-11
 */

#include "cfe_evs_stm32.h"
#include "osal_freertos.h"
#include "cfe_es_stm32.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

/* ══════════════════════════════════════════════════════════════════
 * VARIABLE GLOBAL
 * ══════════════════════════════════════════════════════════════════ */

CFE_EVS_Global_t CFE_EVS_Global;

/* ══════════════════════════════════════════════════════════════════
 * HELPERS INTERNOS
 * ══════════════════════════════════════════════════════════════════ */

static void EVS_Lock(void)
{
    if (CFE_EVS_Global.Initialized)
        OS_MutexLock(CFE_EVS_Global.Mutex);
}

static void EVS_Unlock(void)
{
    if (CFE_EVS_Global.Initialized)
        OS_MutexUnlock(CFE_EVS_Global.Mutex);
}

/** Convierte tipo de evento a string para el prefijo UART */
static const char *EVS_TypeToStr(CFE_EVS_EventType_t type)
{
    switch (type)
    {
        case CFE_EVS_EventType_DEBUG:       return "DEBUG";
        case CFE_EVS_EventType_INFORMATION: return "INFO";
        case CFE_EVS_EventType_ERROR:       return "ERROR";
        case CFE_EVS_EventType_CRITICAL:    return "CRIT";
        default:                            return "????";
    }
}

/**
 * Busca el registro EVS de una app por su AppId.
 * Debe llamarse con el mutex tomado.
 */
static CFE_EVS_AppRecord_t *EVS_FindAppRecord(CFE_ES_AppId_t AppId)
{
    uint32 i;
    for (i = 0u; i < CFE_EVS_MAX_APPS; i++)
    {
        if (CFE_EVS_Global.AppTable[i].InUse &&
            CFE_EVS_Global.AppTable[i].AppId == AppId)
        {
            return &CFE_EVS_Global.AppTable[i];
        }
    }
    return NULL;
}

/**
 * Verifica si un evento debe enviarse según los filtros.
 * Retorna true si debe enviarse, false si está filtrado.
 * Debe llamarse con el mutex tomado.
 *
 * Lógica de filtrado binario (igual que NASA):
 *   - Si no hay filtro para este EventID → enviar siempre
 *   - Si hay filtro: enviar si (FilterCount & Mask) == 0
 *   - Incrementar FilterCount después de evaluar
 */
static bool EVS_CheckFilter(CFE_EVS_AppRecord_t *rec,
                             uint16               EventID,
                             CFE_EVS_EventType_t  EventType)
{
    uint16 i;

    /* Verificar nivel mínimo de evento */
    if (EventType < rec->ActiveLevel)
        return false;

    /* Buscar filtro específico para este EventID */
    for (i = 0u; i < rec->NumFilters; i++)
    {
        if (rec->Filters[i].EventID == EventID)
        {
            bool send = ((rec->FilterCount[i] & rec->Filters[i].Mask) == 0u);
            rec->FilterCount[i]++;
            return send;
        }
    }

    /* Sin filtro específico → enviar siempre */
    return true;
}

/**
 * Implementación común de envío de evento.
 * Formatea el mensaje y lo escribe por UART.
 */
static CFE_Status_t EVS_SendEventImpl(CFE_ES_AppId_t      AppId,
                                       uint16              EventID,
                                       CFE_EVS_EventType_t EventType,
                                       const char         *Spec,
                                       va_list             Args)
{
    CFE_EVS_AppRecord_t *rec;
    char                 msg[CFE_EVS_MAX_MESSAGE_LENGTH];
    char                 app_name[OS_MAX_NAME_LEN];
    bool                 do_send;

    if (!CFE_EVS_Global.Initialized)
    {
        /* EVS no inicializado — imprimir de todas formas sin prefijo */
        vsnprintf(msg, sizeof(msg), Spec, Args);
        OS_printf("[EVS/UNKNOWN/NOINIT] %s\n", msg);
        return CFE_SUCCESS;
    }

    EVS_Lock();

    rec = EVS_FindAppRecord(AppId);

    if (rec == NULL)
    {
        /*
         * App no registrada con EVS.
         * Adaptado de NASA: en el original se descarta silenciosamente.
         * En STM32 lo imprimimos igual para no perder información
         * durante el desarrollo.
         */
        EVS_Unlock();
        vsnprintf(msg, sizeof(msg), Spec, Args);
        OS_printf("[EVS/UNREG/%s] EID=%u %s\n",
                  EVS_TypeToStr(EventType),
                  (unsigned)EventID, msg);
        return CFE_SUCCESS;
    }

    strncpy(app_name, rec->AppName, sizeof(app_name) - 1u);
    app_name[sizeof(app_name) - 1u] = '\0';

    do_send = EVS_CheckFilter(rec, EventID, EventType);

    EVS_Unlock();

    if (!do_send)
        return CFE_SUCCESS;

    /* Formatear el mensaje */
    vsnprintf(msg, sizeof(msg), Spec, Args);

    /*
     * Formato de salida UART:
     *   [EVS/AppName/TIPO] EID=N Mensaje
     *
     * En cFS completo aquí iría CFE_SB_TransmitMsg con el
     * paquete de telemetría EVS. En STM32 fase 1 usamos UART.
     */
    OS_printf("[EVS/%s/%s] EID=%u %s\n",
              app_name,
              EVS_TypeToStr(EventType),
              (unsigned)EventID,
              msg);

    EVS_Lock();
    CFE_EVS_Global.TotalEventsSent++;
    EVS_Unlock();

    return CFE_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_EVS_EarlyInit
 *
 * Inicializa EVS. Llamado por CFE_ES_Main antes de las apps.
 * Adaptado de cfe_evs_task.c CFE_EVS_TaskInit().
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_EVS_EarlyInit(void)
{
    int32 status;

    memset(&CFE_EVS_Global, 0, sizeof(CFE_EVS_Global));

    status = OS_MutexCreate(&CFE_EVS_Global.Mutex, "EVS_MTX", 0u);
    if (status != OS_SUCCESS)
    {
        OS_printf("EVS FATAL: no se pudo crear mutex (%ld)\n", (long)status);
        return CFE_STATUS_EXTERNAL_RESOURCE_FAIL;
    }

    CFE_EVS_Global.Initialized = true;

    OS_printf("EVS: inicializado correctamente\n");
    return CFE_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_EVS_Register
 *
 * Registra la app actual con EVS y configura sus filtros.
 *
 * Adaptado de cfe_evs_task.c CFE_EVS_Register().
 * Diferencias:
 *   - Busca el AppId en ES_Global en lugar de un TaskTable separado
 *   - No hay tabla de configuración por uplink (simplificado)
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_EVS_Register(const CFE_EVS_EventFilter_t *Filters,
                               uint16                       NumFilters,
                               CFE_EVS_FilterScheme_t       FilterScheme)
{
    CFE_ES_AppId_t       AppId;
    CFE_ES_AppRecord_t  *es_rec;
    CFE_EVS_AppRecord_t *evs_rec;
    uint32               slot;
    uint16               i;

    (void)FilterScheme;  /* solo soportamos binario */

    /* Obtener AppId del contexto actual */
    if (CFE_ES_GetAppID(&AppId) != CFE_SUCCESS)
    {
        OS_printf("EVS: Register fallo — no AppID para tarea actual\n");
        return CFE_EVS_APP_NOT_REGISTERED;
    }

    /* Obtener nombre de la app desde ES */
    EVS_Lock();

    /* Verificar si ya está registrada */
    if (EVS_FindAppRecord(AppId) != NULL)
    {
        EVS_Unlock();
        OS_printf("EVS: App AppID=%u ya registrada\n", (unsigned)AppId);
        return CFE_SUCCESS;
    }

    /* Buscar slot libre */
    slot = CFE_EVS_MAX_APPS;
    for (i = 0u; i < CFE_EVS_MAX_APPS; i++)
    {
        if (!CFE_EVS_Global.AppTable[i].InUse)
        {
            slot = i;
            break;
        }
    }

    if (slot == CFE_EVS_MAX_APPS)
    {
        EVS_Unlock();
        OS_printf("EVS: Register fallo — no hay slots libres\n");
        return CFE_EVS_APP_NOT_REGISTERED;
    }

    evs_rec = &CFE_EVS_Global.AppTable[slot];
    memset(evs_rec, 0, sizeof(*evs_rec));

    evs_rec->InUse      = true;
    evs_rec->AppId      = AppId;
    evs_rec->ActiveLevel = CFE_EVS_MIN_LEVEL;

    /* Copiar nombre desde ES_Global */
    es_rec = &CFE_ES_Global.AppTable[(uint32)AppId];
    if (es_rec->InUse)
    {
        strncpy(evs_rec->AppName, es_rec->AppName,
                sizeof(evs_rec->AppName) - 1u);
    }
    else
    {
        snprintf(evs_rec->AppName, sizeof(evs_rec->AppName),
                 "App%u", (unsigned)AppId);
    }

    /* Copiar filtros */
    if (Filters != NULL && NumFilters > 0u)
    {
        uint16 nf = (NumFilters < CFE_EVS_MAX_EVENT_FILTERS)
                    ? NumFilters : CFE_EVS_MAX_EVENT_FILTERS;
        evs_rec->NumFilters = nf;
        for (i = 0u; i < nf; i++)
        {
            evs_rec->Filters[i].EventID = Filters[i].EventID;
            evs_rec->Filters[i].Mask    = Filters[i].Mask;
            evs_rec->FilterCount[i]     = 0u;
        }
    }

    EVS_Unlock();

    OS_printf("EVS: App '%s' registrada (slot=%u, filtros=%u)\n",
              evs_rec->AppName, (unsigned)slot, (unsigned)evs_rec->NumFilters);

    return CFE_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_EVS_SendEvent
 *
 * Envía un evento. AppID se obtiene del contexto de tarea actual.
 * Adaptado de cfe_evs_log.c CFE_EVS_SendEvent().
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_EVS_SendEvent(uint16              EventID,
                                CFE_EVS_EventType_t EventType,
                                const char         *Spec, ...)
{
    CFE_ES_AppId_t AppId;
    CFE_Status_t   status;
    va_list        args;

    if (CFE_ES_GetAppID(&AppId) != CFE_SUCCESS)
        AppId = CFE_ES_APPID_UNDEFINED;

    va_start(args, Spec);
    status = EVS_SendEventImpl(AppId, EventID, EventType, Spec, args);
    va_end(args);

    return status;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_EVS_SendEventWithAppID
 *
 * Envía un evento con AppID explícito.
 * Usado por servicios core (ES, SB) que ya tienen su AppID.
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_EVS_SendEventWithAppID(uint16              EventID,
                                         CFE_EVS_EventType_t EventType,
                                         CFE_ES_AppId_t      AppID,
                                         const char         *Spec, ...)
{
    CFE_Status_t status;
    va_list      args;

    va_start(args, Spec);
    status = EVS_SendEventImpl(AppID, EventID, EventType, Spec, args);
    va_end(args);

    return status;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_EVS_SendTimedEvent
 *
 * Variante con timestamp. En STM32 fase 1 el timestamp se ignora.
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_EVS_SendTimedEvent(uint16              EventID,
                                     CFE_EVS_EventType_t EventType,
                                     const void         *Time,
                                     const char         *Spec, ...)
{
    CFE_ES_AppId_t AppId;
    CFE_Status_t   status;
    va_list        args;

    (void)Time;  /* ignorado en fase 1 */

    if (CFE_ES_GetAppID(&AppId) != CFE_SUCCESS)
        AppId = CFE_ES_APPID_UNDEFINED;

    va_start(args, Spec);
    status = EVS_SendEventImpl(AppId, EventID, EventType, Spec, args);
    va_end(args);

    return status;
}
