/**
 * @file  cfe_evs_stm32.h
 * @brief cFE Event Services — implementación mínima para STM32
 *
 * ── Qué es EVS ───────────────────────────────────────────────────
 *
 *   EVS es el servicio de eventos de cFE. Permite que las apps
 *   generen eventos categorizados (DEBUG, INFO, ERROR, CRITICAL)
 *   con filtrado configurable.
 *
 *   En un cFS completo los eventos van al telemetry downlink.
 *   En STM32 fase 1 van al UART vía OS_printf.
 *
 * ── Interfaz NASA que implementamos ─────────────────────────────
 *
 *   CFE_EVS_Register()           — registrar app con filtros
 *   CFE_EVS_SendEvent()          — enviar evento (AppID desde contexto)
 *   CFE_EVS_SendEventWithAppID() — enviar evento con AppID explícito
 *   CFE_EVS_SendTimedEvent()     — enviar evento con timestamp
 *
 * ── Interfaz NASA que omitimos (fase 1) ─────────────────────────
 *
 *   CFE_EVS_ResetFilter()        — reset de filtros (no urgente)
 *   CFE_EVS_ResetAllFilters()    — reset de todos los filtros
 *   Telemetry output             — requiere SB (fase siguiente)
 *
 * ── Formato de salida UART ──────────────────────────────────────
 *
 *   [EVS/AppName/TIPO] EventID=N Mensaje
 *
 *   Ejemplo:
 *   [EVS/DCMotor/INFO] EventID=1 Motor iniciado correctamente
 *   [EVS/DCMotor/ERROR] EventID=5 Cola de comandos llena
 *
 * @target  STM32F439ZI / STM32H730VBT6
 * @date    2026-04-11
 */

#ifndef CFE_EVS_STM32_H
#define CFE_EVS_STM32_H

#include "osal_freertos.h"
#include "cfe_es_stm32.h"

#include <stdint.h>
#ifndef uint16
typedef uint16_t uint16;
#endif
#ifndef uint32
typedef uint32_t uint32;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════
 * TIPOS DE EVENTO — igual que NASA cFE
 * ══════════════════════════════════════════════════════════════════ */

typedef uint16 CFE_EVS_EventType_t;

#define CFE_EVS_EventType_DEBUG       1u  /**< Debug — normalmente filtrado */
#define CFE_EVS_EventType_INFORMATION 2u  /**< Informativo */
#define CFE_EVS_EventType_ERROR       3u  /**< Error recuperable */
#define CFE_EVS_EventType_CRITICAL    4u  /**< Error crítico */

/* ══════════════════════════════════════════════════════════════════
 * FILTROS DE EVENTO — misma estructura que NASA
 * ══════════════════════════════════════════════════════════════════ */

/** Máscara de filtro: 0x0000 = siempre enviar, 0xFFFF = nunca enviar */
typedef struct {
    uint16 EventID;   /**< ID del evento a filtrar */
    uint16 Mask;      /**< Máscara de filtro binario */
} CFE_EVS_BinFilter_t;

/** Alias de compatibilidad NASA */
typedef CFE_EVS_BinFilter_t CFE_EVS_EventFilter_t;

/** Tipo de esquema de filtro */
typedef uint16 CFE_EVS_FilterScheme_t;
#define CFE_EVS_EventFilter_BINARY  0u  /**< Filtro binario (único soportado) */

/* ══════════════════════════════════════════════════════════════════
 * LIMITES
 * ══════════════════════════════════════════════════════════════════ */

#define CFE_EVS_MAX_EVENT_FILTERS    8u   /**< Filtros por app */
#define CFE_EVS_MAX_APPS             CFE_PLATFORM_ES_MAX_APPLICATIONS
#define CFE_EVS_MAX_MESSAGE_LENGTH   122u /**< Máx chars del mensaje */

/** Nivel mínimo de evento que se imprime (DEBUG=1, INFO=2, ERROR=3, CRIT=4) */
#define CFE_EVS_MIN_LEVEL  CFE_EVS_EventType_DEBUG

/* ══════════════════════════════════════════════════════════════════
 * REGISTRO DE APP EN EVS
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    bool                InUse;
    CFE_ES_AppId_t      AppId;
    char                AppName[OS_MAX_NAME_LEN];
    uint16              ActiveLevel;    /**< Nivel mínimo activo */
    uint16              NumFilters;
    CFE_EVS_BinFilter_t Filters[CFE_EVS_MAX_EVENT_FILTERS];
    uint16              FilterCount[CFE_EVS_MAX_EVENT_FILTERS]; /**< veces enviado */
} CFE_EVS_AppRecord_t;

/* ══════════════════════════════════════════════════════════════════
 * ESTADO GLOBAL
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    bool               Initialized;
    CFE_EVS_AppRecord_t AppTable[CFE_EVS_MAX_APPS];
    osal_id_t          Mutex;
    uint32             TotalEventsSent;
} CFE_EVS_Global_t;

extern CFE_EVS_Global_t CFE_EVS_Global;

/* ══════════════════════════════════════════════════════════════════
 * API PÚBLICA — compatible con NASA cFE
 * ══════════════════════════════════════════════════════════════════ */

/**
 * @brief Inicializar EVS — llamado por CFE_ES_Main antes de las apps
 */
CFE_Status_t CFE_EVS_EarlyInit(void);

/**
 * @brief Registrar la app actual con EVS
 *
 * Debe llamarse al inicio de cada app, antes de SendEvent.
 * Equivalente a CFE_EVS_Register() de NASA.
 *
 * @param Filters     Array de filtros (puede ser NULL)
 * @param NumFilters  Número de filtros en el array
 * @param FilterScheme Esquema (usar CFE_EVS_EventFilter_BINARY)
 */
CFE_Status_t CFE_EVS_Register(const CFE_EVS_EventFilter_t *Filters,
                               uint16                       NumFilters,
                               CFE_EVS_FilterScheme_t       FilterScheme);

/**
 * @brief Enviar evento — AppID se obtiene del contexto de tarea actual
 *
 * @param EventID   ID del evento (definido por la app)
 * @param EventType CFE_EVS_EventType_DEBUG/INFORMATION/ERROR/CRITICAL
 * @param Spec      Formato printf del mensaje
 */
CFE_Status_t CFE_EVS_SendEvent(uint16              EventID,
                                CFE_EVS_EventType_t EventType,
                                const char         *Spec, ...);

/**
 * @brief Enviar evento con AppID explícito
 *
 * Variante usada por servicios core (ES, SB) que conocen su AppID.
 */
CFE_Status_t CFE_EVS_SendEventWithAppID(uint16              EventID,
                                         CFE_EVS_EventType_t EventType,
                                         CFE_ES_AppId_t      AppID,
                                         const char         *Spec, ...);

/**
 * @brief Enviar evento con timestamp explícito
 *
 * En STM32 fase 1 el timestamp es ignorado — usamos HAL_GetTick().
 */
CFE_Status_t CFE_EVS_SendTimedEvent(uint16              EventID,
                                     CFE_EVS_EventType_t EventType,
                                     const void         *Time,
                                     const char         *Spec, ...);

#ifdef __cplusplus
}
#endif

#endif /* CFE_EVS_STM32_H */
