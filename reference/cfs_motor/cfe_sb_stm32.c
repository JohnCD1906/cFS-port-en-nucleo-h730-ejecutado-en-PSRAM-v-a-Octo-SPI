/**
 * @file  cfe_sb_stm32.c
 * @brief cFE Software Bus — implementación mínima para STM32
 *
 * ── Origen del código ────────────────────────────────────────────
 *
 *   Adaptado de NASA cFS Draco (Apache 2.0):
 *     cfe/modules/sb/fsw/src/cfe_sb_api.c   → CreatePipe, Subscribe, Transmit
 *     cfe/modules/sb/fsw/src/cfe_sb_util.c  → routing table
 *
 *   Cambios respecto al original NASA:
 *     - Pool de buffers usa CFE_ES_PoolCreate sobre CCMRAM
 *     - Pipes implementadas con OS_QueueCreate de nuestro OSAL
 *     - Routing table es array estático (no linked list)
 *     - Eliminado: telemetría de estadísticas SB (requiere SB circular)
 *     - Eliminado: zero-copy API (fase siguiente)
 *
 * ── Flujo de un mensaje ──────────────────────────────────────────
 *
 *   TransmitMsg(msg)
 *     → buscar MsgId en RouteTable
 *     → para cada destino suscrito:
 *         → OS_QueuePut(pipe_queue, &ptr, sizeof(ptr))
 *     → ReceiveBuffer(pipe)
 *         → OS_QueueGet(pipe_queue, &ptr, ...)
 *         → retornar puntero al mensaje
 *
 *   NOTA: en esta implementación el mensaje se copia directamente
 *   a la cola (no por referencia) para simplicidad. Para mensajes
 *   grandes usar zero-copy en fase siguiente.
 *
 * @target  STM32F439ZI / STM32H730VBT6
 * @date    2026-04-11
 */

#include "cfe_sb_stm32.h"
#include "cfe_es_stm32.h"
#include "cfe_evs_stm32.h"
#include "osal_freertos.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* ══════════════════════════════════════════════════════════════════
 * VARIABLE GLOBAL
 * ══════════════════════════════════════════════════════════════════ */

CFE_SB_Global_t CFE_SB_Global;

/* ══════════════════════════════════════════════════════════════════
 * HELPERS INTERNOS
 * ══════════════════════════════════════════════════════════════════ */

static void SB_Lock(void)
{
    if (CFE_SB_Global.Initialized)
        OS_MutexLock(CFE_SB_Global.Mutex);
}

static void SB_Unlock(void)
{
    if (CFE_SB_Global.Initialized)
        OS_MutexUnlock(CFE_SB_Global.Mutex);
}

/** Buscar entrada en RouteTable por MsgId. Debe llamarse con mutex. */
static CFE_SB_RouteEntry_t *SB_FindRoute(CFE_SB_MsgId_t MsgId)
{
    uint32 i;
    for (i = 0u; i < CFE_PLATFORM_SB_MAX_MSG_IDS; i++)
    {
        if (CFE_SB_Global.RouteTable[i].InUse &&
            CFE_SB_Global.RouteTable[i].MsgId == MsgId)
            return &CFE_SB_Global.RouteTable[i];
    }
    return NULL;
}

/** Buscar o crear entrada en RouteTable. Debe llamarse con mutex. */
static CFE_SB_RouteEntry_t *SB_FindOrCreateRoute(CFE_SB_MsgId_t MsgId)
{
    CFE_SB_RouteEntry_t *entry = SB_FindRoute(MsgId);
    uint32 i;

    if (entry != NULL)
        return entry;

    /* Buscar slot libre */
    for (i = 0u; i < CFE_PLATFORM_SB_MAX_MSG_IDS; i++)
    {
        if (!CFE_SB_Global.RouteTable[i].InUse)
        {
            entry = &CFE_SB_Global.RouteTable[i];
            memset(entry, 0, sizeof(*entry));
            entry->InUse    = true;
            entry->MsgId    = MsgId;
            entry->NumDests = 0u;
            return entry;
        }
    }
    return NULL;
}

/** Buscar registro de pipe por ID. Debe llamarse con mutex. */
static CFE_SB_PipeRecord_t *SB_FindPipe(CFE_SB_PipeId_t PipeId)
{
    if (PipeId >= CFE_PLATFORM_SB_MAX_PIPES)
        return NULL;
    if (!CFE_SB_Global.PipeTable[PipeId].InUse)
        return NULL;
    return &CFE_SB_Global.PipeTable[PipeId];
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_SB_EarlyInit
 *
 * Inicializa el SB. Llamado por CFE_ES_Main en CORE_STARTUP,
 * después de CFE_EVS_EarlyInit().
 *
 * Adaptado de cfe_sb_api.c CFE_SB_TaskInit().
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_SB_EarlyInit(void)
{
    int32 status;

    memset(&CFE_SB_Global, 0, sizeof(CFE_SB_Global));

    status = OS_MutexCreate(&CFE_SB_Global.Mutex, "SB_MTX", 0u);
    if (status != OS_SUCCESS)
    {
        OS_printf("SB FATAL: no se pudo crear mutex (%ld)\n", (long)status);
        return CFE_SB_BAD_ARGUMENT;
    }

    /*
     * Crear pool de buffers en el heap de FreeRTOS.
     * En una versión futura esto irá en CCMRAM via CFE_ES_PoolCreate.
     * Por ahora usamos un array estático en BSS para simplicidad.
     */
    status = CFE_ES_PoolCreateNoSem(&CFE_SB_Global.BufPoolHandle,
                                     CFE_SB_Global.BufPool,
                                     CFE_PLATFORM_SB_BUF_MEMORY_BYTES);
    if (status != CFE_SUCCESS)
    {
        OS_printf("SB WARN: pool de buffers no disponible (%ld)\n",
                  (long)status);
        /* No es fatal — seguimos sin pool */
    }

    CFE_SB_Global.Initialized = true;

    OS_printf("SB: inicializado (max_pipes=%u, max_msgids=%u, pool=%u bytes)\n",
              (unsigned)CFE_PLATFORM_SB_MAX_PIPES,
              (unsigned)CFE_PLATFORM_SB_MAX_MSG_IDS,
              (unsigned)CFE_PLATFORM_SB_BUF_MEMORY_BYTES);

    return CFE_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_SB_CreatePipe
 *
 * Adaptado de cfe_sb_api.c CFE_SB_CreatePipe().
 * Diferencias:
 *   - Usa OS_QueueCreate de nuestro OSAL en lugar de OSAL completo
 *   - Tamaño de item = sizeof(puntero) para pasar referencias
 *     (en esta fase copiamos el mensaje completo: sizeof CFE_MSG_Message_t)
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_SB_CreatePipe(CFE_SB_PipeId_t *PipeIdPtr,
                                uint16_t           Depth,
                                const char      *PipeName)
{
    CFE_SB_PipeRecord_t *prec;
    CFE_ES_AppId_t       AppId;
    osal_id_t            QueueId;
    uint32               slot;
    int32                status;
    char                 qname[OS_MAX_NAME_LEN];

    if (PipeIdPtr == NULL || PipeName == NULL || Depth == 0u)
        return CFE_SB_BAD_ARGUMENT;

    *PipeIdPtr = CFE_SB_INVALID_PIPE;

    /* Obtener AppId del contexto actual */
    if (CFE_ES_GetAppID(&AppId) != CFE_SUCCESS)
        AppId = CFE_ES_APPID_UNDEFINED;

    SB_Lock();

    /* Buscar slot libre */
    slot = CFE_PLATFORM_SB_MAX_PIPES;
    for (uint32 i = 0u; i < CFE_PLATFORM_SB_MAX_PIPES; i++)
    {
        if (!CFE_SB_Global.PipeTable[i].InUse)
        {
            slot = i;
            break;
        }
    }

    if (slot == CFE_PLATFORM_SB_MAX_PIPES)
    {
        SB_Unlock();
        OS_printf("SB: CreatePipe '%s' fallo — sin slots\n", PipeName);
        return CFE_SB_MAX_PIPES_MET;
    }

    SB_Unlock();

    /* Crear la cola OSAL — nombre único con prefijo SB_ */
    snprintf(qname, sizeof(qname), "SB_%s", PipeName);

    /*
     * Item size = sizeof(CFE_MSG_Message_t).
     * En esta fase copiamos el mensaje completo en la cola.
     * En fase zero-copy sería sizeof(puntero).
     */
    status = OS_QueueCreate(&QueueId, qname, Depth,
                             CFE_SB_MAX_MSG_SIZE, 0u);
    if (status != OS_SUCCESS)
    {
        OS_printf("SB: CreatePipe '%s' — OS_QueueCreate fallo (%ld)\n",
                  PipeName, (long)status);
        return CFE_SB_MAX_PIPES_MET;
    }

    SB_Lock();

    prec = &CFE_SB_Global.PipeTable[slot];
    memset(prec, 0, sizeof(*prec));
    prec->InUse   = true;
    prec->PipeId  = (CFE_SB_PipeId_t)slot;
    prec->QueueId = QueueId;
    prec->Depth   = Depth;
    prec->AppId   = AppId;
    strncpy(prec->Name, PipeName, sizeof(prec->Name) - 1u);

    *PipeIdPtr = (CFE_SB_PipeId_t)slot;

    SB_Unlock();

    OS_printf("SB: Pipe '%s' creada (id=%lu, depth=%u)\n",
              PipeName, (unsigned long)slot, (unsigned)Depth);

    return CFE_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_SB_DeletePipe
 *
 * Adaptado de cfe_sb_api.c CFE_SB_DeletePipe().
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_SB_DeletePipe(CFE_SB_PipeId_t PipeId)
{
    CFE_SB_PipeRecord_t *prec;
    CFE_SB_RouteEntry_t *route;
    uint32 i;

    SB_Lock();

    prec = SB_FindPipe(PipeId);
    if (prec == NULL)
    {
        SB_Unlock();
        return CFE_SB_PIPE_NOT_FOUND;
    }

    /* Eliminar todas las suscripciones de esta pipe */
    for (i = 0u; i < CFE_PLATFORM_SB_MAX_MSG_IDS; i++)
    {
        route = &CFE_SB_Global.RouteTable[i];
        if (!route->InUse) continue;
        int j;
        for (j = 0u; j < route->NumDests; j++)
        {
            if (route->Dests[j].PipeId == PipeId)
            {
                /* Mover el último destino a esta posición */
                route->NumDests--;
                if (j < route->NumDests)
                    route->Dests[j] = route->Dests[route->NumDests];
                memset(&route->Dests[route->NumDests], 0,
                       sizeof(route->Dests[0]));
                break;
            }
        }

        /* Si no quedan destinos, liberar la ruta */
        if (route->NumDests == 0u)
        {
            route->InUse = false;
            route->MsgId = CFE_SB_INVALID_MSG_ID;
        }
    }

    /* Eliminar la cola OSAL */
    OS_QueueDelete(prec->QueueId);

    OS_printf("SB: Pipe '%s' eliminada\n", prec->Name);

    memset(prec, 0, sizeof(*prec));

    SB_Unlock();
    return CFE_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_SB_Subscribe
 *
 * Adaptado de cfe_sb_api.c CFE_SB_Subscribe().
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_SB_Subscribe(CFE_SB_MsgId_t  MsgId,
                               CFE_SB_PipeId_t PipeId)
{
    CFE_SB_RouteEntry_t *route;
    CFE_SB_PipeRecord_t *prec;
    uint32 j;

    if (MsgId == CFE_SB_INVALID_MSG_ID)
        return CFE_SB_BAD_ARGUMENT;

    SB_Lock();

    prec = SB_FindPipe(PipeId);
    if (prec == NULL)
    {
        SB_Unlock();
        OS_printf("SB: Subscribe — pipe %lu no encontrada\n",
                  (unsigned long)PipeId);
        return CFE_SB_PIPE_NOT_FOUND;
    }

    route = SB_FindOrCreateRoute(MsgId);
    if (route == NULL)
    {
        SB_Unlock();
        OS_printf("SB: Subscribe — routing table llena (MsgId=0x%04X)\n",
                  (unsigned)MsgId);
        return CFE_SB_MAX_MSGS_MET;
    }

    /* Verificar si ya está suscrito */
    for (j = 0u; j < route->NumDests; j++)
    {
        if (route->Dests[j].PipeId == PipeId)
        {
            SB_Unlock();
            OS_printf("SB: Subscribe — pipe '%s' ya suscrita a MsgId=0x%04X\n",
                      prec->Name, (unsigned)MsgId);
            return CFE_SUCCESS;
        }
    }

    if (route->NumDests >= CFE_PLATFORM_SB_MAX_DEST_PER_PKT)
    {
        SB_Unlock();
        OS_printf("SB: Subscribe — max destinos para MsgId=0x%04X\n",
                  (unsigned)MsgId);
        return CFE_SB_MAX_MSGS_MET;
    }

    /* Agregar destino */
    route->Dests[route->NumDests].PipeId   = PipeId;
    route->Dests[route->NumDests].MsgLimit = CFE_PLATFORM_SB_DEFAULT_MSG_LIMIT;
    route->Dests[route->NumDests].MsgCount = 0u;
    route->NumDests++;

    SB_Unlock();

    OS_printf("SB: Pipe '%s' suscrita a MsgId=0x%04X\n",
              prec->Name, (unsigned)MsgId);

    return CFE_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_SB_Unsubscribe
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_SB_Unsubscribe(CFE_SB_MsgId_t  MsgId,
                                 CFE_SB_PipeId_t PipeId)
{
    CFE_SB_RouteEntry_t *route;
    uint32 j;

    SB_Lock();

    route = SB_FindRoute(MsgId);
    if (route == NULL)
    {
        SB_Unlock();
        return CFE_SB_NO_SUBSCRIBERS;
    }

    for (j = 0u; j < route->NumDests; j++)
    {
        if (route->Dests[j].PipeId == PipeId)
        {
            route->NumDests--;
            if (j < route->NumDests)
                route->Dests[j] = route->Dests[route->NumDests];
            memset(&route->Dests[route->NumDests], 0,
                   sizeof(route->Dests[0]));

            if (route->NumDests == 0u)
            {
                route->InUse = false;
                route->MsgId = CFE_SB_INVALID_MSG_ID;
            }
            SB_Unlock();
            return CFE_SUCCESS;
        }
    }

    SB_Unlock();
    return CFE_SB_NO_SUBSCRIBERS;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_SB_TransmitMsg
 *
 * Adaptado de cfe_sb_api.c CFE_SB_TransmitMsg().
 * Diferencias:
 *   - Copia el mensaje directamente a la cola (sin zero-copy)
 *   - No usa el pool de buffers en esta fase (copia directa)
 *
 * Esto significa que el payload máximo es sizeof(CFE_MSG_Message_t).
 * Para mensajes más grandes usar CFE_SB_AllocateMessageBuffer (fase 2).
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_SB_TransmitMsg(CFE_MSG_Message_t *MsgPtr,
                                 bool               IncrSeqCnt)
{
    CFE_SB_RouteEntry_t *route;
    CFE_SB_PipeRecord_t *prec;
    uint32               i;
    int32                status;
    uint32               sent = 0u;

    if (MsgPtr == NULL)
        return CFE_SB_BAD_ARGUMENT;

    if (MsgPtr->Length < sizeof(CFE_MSG_Message_t) ||
        MsgPtr->Length > CFE_SB_MAX_MSG_SIZE)
        return CFE_SB_MSG_TOO_BIG;

    SB_Lock();

    route = SB_FindRoute(MsgPtr->MsgId);
    if (route == NULL || route->NumDests == 0u)
    {
        CFE_SB_Global.NoSubscribers++;
        SB_Unlock();
        return CFE_SB_NO_SUBSCRIBERS;
    }

    /* Incrementar secuencia */
    if (IncrSeqCnt)
    {
        route->SeqCount++;
        MsgPtr->Sequence = route->SeqCount;
    }

    /* Enviar a todos los destinos */
    for (i = 0u; i < route->NumDests; i++)
    {
        prec = SB_FindPipe(route->Dests[i].PipeId);
        if (prec == NULL) continue;

        /* Verificar message limit */
        if (route->Dests[i].MsgCount >= route->Dests[i].MsgLimit)
        {
            OS_printf("SB: TransmitMsg — pipe '%s' llena (MsgId=0x%04X)\n",
                      prec->Name, (unsigned)MsgPtr->MsgId);
            continue;
        }

        /*
         * Copiar el mensaje a la cola OSAL.
         * OS_QueuePut copia los datos — el remitente puede
         * modificar MsgPtr después de esta llamada.
         */
        status = OS_QueuePut(prec->QueueId,
                             MsgPtr,
                             (uint32)MsgPtr->Length,
                             0u);
        if (status == OS_SUCCESS)
        {
            route->Dests[i].MsgCount++;
            sent++;
        }
        else
        {
            OS_printf("SB: TransmitMsg — OS_QueuePut a '%s' fallo (%ld)\n",
                      prec->Name, (long)status);
        }
    }

    CFE_SB_Global.MsgsSent++;
    SB_Unlock();

    return (sent > 0u) ? CFE_SUCCESS : CFE_SB_PIPE_FULL;
}

/* ══════════════════════════════════════════════════════════════════
 * CFE_SB_ReceiveBuffer
 *
 * Adaptado de cfe_sb_api.c CFE_SB_ReceiveBuffer().
 * ══════════════════════════════════════════════════════════════════ */
CFE_Status_t CFE_SB_ReceiveBuffer(CFE_SB_Buffer_t *BufPtr,
                                   CFE_SB_PipeId_t  PipeId,
                                   uint32           TimeoutMs)
{
    CFE_SB_PipeRecord_t *prec;
    CFE_SB_RouteEntry_t *route;
    /* Buffer estático con tamaño máximo para cualquier mensaje SB */
    static uint8_t rx_buf[CFE_SB_MAX_MSG_SIZE];
    size_t size_copied;
    int32  status;
    uint32 i;

    if (BufPtr == NULL)
        return CFE_SB_BAD_ARGUMENT;

    *BufPtr = NULL;

    SB_Lock();
    prec = SB_FindPipe(PipeId);
    SB_Unlock();

    if (prec == NULL)
        return CFE_SB_PIPE_NOT_FOUND;

    /* Convertir timeout */
    uint32 osal_timeout;
    if (TimeoutMs == CFE_SB_POLL)
        osal_timeout = OS_CHECK;
    else if (TimeoutMs == CFE_SB_PEND_FOREVER)
        osal_timeout = OS_PEND;
    else
        osal_timeout = TimeoutMs;

    status = OS_QueueGet(prec->QueueId,
                         &rx_buf,
                         sizeof(rx_buf),
                         &size_copied,
                         osal_timeout);

    if (status == OS_SUCCESS)
    {
        *BufPtr = (CFE_SB_Buffer_t)(void *)rx_buf;

        /* Decrementar MsgCount en la routing table */
        SB_Lock();
        route = SB_FindRoute(((CFE_MSG_Message_t *)(void *)rx_buf)->MsgId);
        if (route != NULL)
        {
            for (i = 0u; i < route->NumDests; i++)
            {
                if (route->Dests[i].PipeId == PipeId)
                {
                    if (route->Dests[i].MsgCount > 0u)
                        route->Dests[i].MsgCount--;
                    break;
                }
            }
        }
        CFE_SB_Global.MsgsReceived++;
        SB_Unlock();

        return CFE_SUCCESS;
    }
    else if (status == OS_QUEUE_EMPTY || status == OS_QUEUE_TIMEOUT)
    {
        return (TimeoutMs == CFE_SB_POLL) ? CFE_SB_NO_MESSAGE
                                           : CFE_SB_TIME_OUT;
    }

    return CFE_SB_PIPE_NOT_FOUND;
}
