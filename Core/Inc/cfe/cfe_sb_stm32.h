/**
 * @file  cfe_sb_stm32.h
 * @brief cFE Software Bus — implementación mínima para STM32
 *
 * ── Qué es el SB ────────────────────────────────────────────────
 *
 *   El Software Bus es el sistema nervioso de cFS. Las apps
 *   publican mensajes con MsgId y se suscriben a MsgIds.
 *   El SB enruta los mensajes a todos los suscriptores sin que
 *   las apps se conozcan entre sí.
 *
 * ── Modelo de implementación STM32 ──────────────────────────────
 *
 *   En cFS completo el SB usa un memory pool de ES para buffers.
 *   En STM32 usamos el pool de ES que ya existe en CCMRAM.
 *
 *   Cada pipe es una cola OSAL (OS_QueueCreate).
 *   La routing table es un array estático de MsgId → pipes.
 *
 * ── API NASA que implementamos ───────────────────────────────────
 *
 *   CFE_SB_CreatePipe()      — crear pipe (cola receptora)
 *   CFE_SB_DeletePipe()      — eliminar pipe
 *   CFE_SB_Subscribe()       — suscribir pipe a un MsgId
 *   CFE_SB_Unsubscribe()     — quitar suscripción
 *   CFE_SB_TransmitMsg()     — enviar mensaje
 *   CFE_SB_ReceiveBuffer()   — recibir mensaje (con timeout)
 *   CFE_SB_AllocateMessageBuffer() — obtener buffer del pool
 *   CFE_SB_ReleaseMessageBuffer()  — liberar buffer al pool
 *
 * ── Formato de mensaje SB ────────────────────────────────────────
 *
 *   Todo mensaje SB tiene un header CFE_MSG_Message_t al inicio.
 *   El header contiene el MsgId y el tamaño total.
 *   El payload sigue inmediatamente después del header.
 *
 * ── Límites para STM32F439ZI ────────────────────────────────────
 *
 *   CFE_PLATFORM_SB_MAX_PIPES        8   pipes simultáneas
 *   CFE_PLATFORM_SB_MAX_MSG_IDS      16  MsgIds únicos
 *   CFE_PLATFORM_SB_MAX_DEST_PER_PKT 4   destinos por MsgId
 *   CFE_PLATFORM_SB_BUF_MEMORY_BYTES 4096 bytes para el pool SB
 *
 * @target  STM32F439ZI / STM32H730VBT6
 * @date    2026-04-11
 */

#ifndef CFE_SB_STM32_H
#define CFE_SB_STM32_H

#include "osal/osal_freertos.h"
#include "cfe/cfe_es_stm32.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════
 * LIMITES DE PLATAFORMA
 * ══════════════════════════════════════════════════════════════════ */

#define CFE_PLATFORM_SB_MAX_PIPES           8u
#define CFE_PLATFORM_SB_MAX_MSG_IDS        16u
#define CFE_PLATFORM_SB_MAX_DEST_PER_PKT    4u
#define CFE_PLATFORM_SB_DEFAULT_MSG_LIMIT   4u
#define CFE_PLATFORM_SB_BUF_MEMORY_BYTES   (4u * 1024u)

/** Tamaño máximo de un mensaje SB (header + payload) en bytes.
 *  Todos los mensajes de la app deben caber en este límite.
 *  DC_Cmd_t = 12 bytes payload + 8 header = 20 bytes → 64 es suficiente. */
#define CFE_SB_MAX_MSG_SIZE                 64u

/* ══════════════════════════════════════════════════════════════════
 * TIPOS FUNDAMENTALES
 * ══════════════════════════════════════════════════════════════════ */

/** MsgId — identificador de mensaje SB */
typedef uint16_t CFE_SB_MsgId_t;

/** Valor inválido de MsgId */
#define CFE_SB_INVALID_MSG_ID  ((CFE_SB_MsgId_t)0xFFFFu)

/** PipeId — identificador de pipe */
typedef uint32 CFE_SB_PipeId_t;

/** Valor inválido de PipeId */
#define CFE_SB_INVALID_PIPE    ((CFE_SB_PipeId_t)0xFFFFFFFFu)

/* ══════════════════════════════════════════════════════════════════
 * HEADER DE MENSAJE SB
 *
 * Todos los mensajes SB empiezan con este header.
 * Compatible con NASA CFE_MSG_Message_t simplificado.
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    CFE_SB_MsgId_t  MsgId;    /**< Identificador del mensaje */
    uint16_t          Length;   /**< Tamaño total en bytes (header + payload) */
    uint32          Sequence; /**< Contador de secuencia (rellena SB) */
} CFE_MSG_Message_t;

/** Puntero a mensaje SB */
typedef CFE_MSG_Message_t *CFE_SB_Buffer_t;

/* ══════════════════════════════════════════════════════════════════
 * TIMEOUT
 * ══════════════════════════════════════════════════════════════════ */

#define CFE_SB_POLL            0u          /**< No bloquear */
#define CFE_SB_PEND_FOREVER    0xFFFFFFFFu /**< Bloquear indefinidamente */

/* ══════════════════════════════════════════════════════════════════
 * CODIGOS DE ERROR SB
 * ══════════════════════════════════════════════════════════════════ */

#define CFE_SB_BAD_ARGUMENT          (-30)
#define CFE_SB_MAX_PIPES_MET         (-31)
#define CFE_SB_PIPE_NOT_FOUND        (-32)
#define CFE_SB_MSG_TOO_BIG           (-33)
#define CFE_SB_NO_SUBSCRIBERS        (-34)
#define CFE_SB_PIPE_FULL             (-35)
#define CFE_SB_TIME_OUT              (-36)
#define CFE_SB_NO_MESSAGE            (-37)
#define CFE_SB_BUFFER_INVALID        (-38)
#define CFE_SB_MAX_MSGS_MET          (-39)

/* ══════════════════════════════════════════════════════════════════
 * ESTRUCTURAS INTERNAS
 * ══════════════════════════════════════════════════════════════════ */

/** Entrada de destino en la routing table */
typedef struct {
    CFE_SB_PipeId_t PipeId;
    uint16_t          MsgLimit;   /**< Máx msgs pendientes en este pipe */
    uint16_t          MsgCount;   /**< Msgs actualmente en el pipe */
} CFE_SB_DestEntry_t;

/** Entrada en la routing table — una por MsgId */
typedef struct {
    bool            InUse;
    CFE_SB_MsgId_t  MsgId;
    uint32          NumDests;
    CFE_SB_DestEntry_t Dests[CFE_PLATFORM_SB_MAX_DEST_PER_PKT];
    uint32          SeqCount; /**< Contador de secuencia para este MsgId */
} CFE_SB_RouteEntry_t;

/** Registro de pipe */
typedef struct {
    bool            InUse;
    CFE_SB_PipeId_t PipeId;
    osal_id_t       QueueId;  /**< Cola OSAL subyacente */
    uint16_t          Depth;
    char            Name[OS_MAX_NAME_LEN];
    CFE_ES_AppId_t  AppId;    /**< App propietaria */
} CFE_SB_PipeRecord_t;

/** Buffer de mensaje en el pool SB */
typedef struct {
    bool    InUse;
    uint16_t  RefCount; /**< Número de pipes que aún no leyeron este buffer */
    uint8_t   Data[sizeof(CFE_MSG_Message_t) + 128u]; /**< header + payload */
} CFE_SB_BufferRecord_t;

/** Estado global del SB */
typedef struct {
    bool                 Initialized;
    osal_id_t            Mutex;
    CFE_SB_PipeRecord_t  PipeTable[CFE_PLATFORM_SB_MAX_PIPES];
    CFE_SB_RouteEntry_t  RouteTable[CFE_PLATFORM_SB_MAX_MSG_IDS];
    uint32               MsgsSent;
    uint32               MsgsReceived;
    uint32               NoSubscribers;

    /* Pool de buffers para mensajes en tránsito */
    uint8_t                BufPool[CFE_PLATFORM_SB_BUF_MEMORY_BYTES];
    CFE_ES_MemHandle_t   BufPoolHandle;
} CFE_SB_Global_t;

extern CFE_SB_Global_t CFE_SB_Global;

/* ══════════════════════════════════════════════════════════════════
 * API PUBLICA
 * ══════════════════════════════════════════════════════════════════ */

/** Inicializar SB — llamado por CFE_ES_Main en CORE_STARTUP */
CFE_Status_t CFE_SB_EarlyInit(void);

/**
 * @brief Crear una pipe (cola receptora de mensajes)
 *
 * @param PipeIdPtr  [out] ID asignado a la pipe
 * @param Depth      Profundidad de la cola (máx mensajes pendientes)
 * @param PipeName   Nombre de la pipe
 */
CFE_Status_t CFE_SB_CreatePipe(CFE_SB_PipeId_t *PipeIdPtr,
                                uint16_t           Depth,
                                const char      *PipeName);

/**
 * @brief Eliminar una pipe
 */
CFE_Status_t CFE_SB_DeletePipe(CFE_SB_PipeId_t PipeId);

/**
 * @brief Suscribir una pipe a un MsgId
 *
 * @param MsgId   MsgId al que suscribirse
 * @param PipeId  Pipe que recibirá los mensajes
 */
CFE_Status_t CFE_SB_Subscribe(CFE_SB_MsgId_t  MsgId,
                               CFE_SB_PipeId_t PipeId);

/**
 * @brief Cancelar suscripción
 */
CFE_Status_t CFE_SB_Unsubscribe(CFE_SB_MsgId_t  MsgId,
                                 CFE_SB_PipeId_t PipeId);

/**
 * @brief Transmitir un mensaje a todos los suscriptores
 *
 * @param MsgPtr     Puntero al mensaje (debe empezar con CFE_MSG_Message_t)
 * @param IncrSeqCnt Incrementar el contador de secuencia
 */
CFE_Status_t CFE_SB_TransmitMsg(CFE_MSG_Message_t *MsgPtr,
                                 bool               IncrSeqCnt);

/**
 * @brief Recibir un mensaje de una pipe
 *
 * @param BufPtr    [out] Puntero al mensaje recibido
 * @param PipeId    Pipe de la que recibir
 * @param TimeoutMs CFE_SB_POLL, CFE_SB_PEND_FOREVER, o ms de timeout
 */
CFE_Status_t CFE_SB_ReceiveBuffer(CFE_SB_Buffer_t *BufPtr,
                                   CFE_SB_PipeId_t  PipeId,
                                   uint32           TimeoutMs);

/**
 * @brief Helpers para acceder al header del mensaje
 */
static inline CFE_SB_MsgId_t CFE_SB_GetMsgId(const CFE_MSG_Message_t *MsgPtr)
{
    return MsgPtr ? MsgPtr->MsgId : CFE_SB_INVALID_MSG_ID;
}

static inline uint16_t CFE_SB_GetTotalMsgLength(const CFE_MSG_Message_t *MsgPtr)
{
    return MsgPtr ? MsgPtr->Length : 0u;
}

static inline void CFE_SB_SetMsgId(CFE_MSG_Message_t *MsgPtr,
                                    CFE_SB_MsgId_t     MsgId)
{
    if (MsgPtr) MsgPtr->MsgId = MsgId;
}

static inline void CFE_SB_SetTotalMsgLength(CFE_MSG_Message_t *MsgPtr,
                                             uint16_t             Length)
{
    if (MsgPtr) MsgPtr->Length = Length;
}

/**
 * @brief Inicializar un mensaje con MsgId y tamaño
 *
 * Equivalente a CFE_MSG_Init() de NASA.
 */
static inline void CFE_SB_InitMsg(CFE_MSG_Message_t *MsgPtr,
                                   CFE_SB_MsgId_t     MsgId,
                                   uint16_t             Length)
{
    if (!MsgPtr) return;
    MsgPtr->MsgId    = MsgId;
    MsgPtr->Length   = Length;
    MsgPtr->Sequence = 0u;
}

#ifdef __cplusplus
}
#endif

#endif /* CFE_SB_STM32_H */
