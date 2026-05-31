/**
 * @file    osal_freertos.h
 * @brief   Port de OSAL (Operating System Abstraction Layer) sobre FreeRTOS
 *          para STM32H730IBT6Q — Cortex-M7 @ 550 MHz
 *
 * Este archivo define los tipos y APIs de OSAL que se mapean directamente
 * a primitivas de FreeRTOS. El objetivo es que las aplicaciones escritas
 * para cFS en Linux funcionen sin modificación sobre FreeRTOS.
 *
 * Mapeo de APIs:
 *   OS_TaskCreate    --> xTaskCreate
 *   OS_QueueCreate   --> xQueueCreate
 *   OS_QueuePut      --> xQueueSend
 *   OS_QueueGet      --> xQueueReceive
 *   OS_MutexCreate   --> xSemaphoreCreateMutex
 *   OS_MutexLock     --> xSemaphoreTake
 *   OS_MutexUnlock   --> xSemaphoreGive
 *   OS_TaskDelay     --> vTaskDelay
 *   OS_printf        --> UART_printf (via UART3)
 *
 * @target  STM32H730IBT6Q
 * @rtos    FreeRTOS v10.x
 * @date    2026-03-08
 */

#ifndef OSAL_FREERTOS_H
#define OSAL_FREERTOS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── Includes FreeRTOS ─────────────────────────────────────────────── */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ── Constantes de retorno ─────────────────────────────────────────── */
#define OS_SUCCESS          ( 0)
#define OS_ERROR            (-1)
#define OS_INVALID_POINTER  (-2)
#define OS_ERR_NAME_TOO_LONG (-3)
#define OS_QUEUE_FULL       (-4)
#define OS_QUEUE_EMPTY      (-5)
#define OS_QUEUE_TIMEOUT    (-6)
#define OS_ERR_INVALID_ID    (-27)
#define OS_ERR_INVALID_SIZE  (-28)
#define OS_ERR_NO_FREE_IDS  (-7)

/* ── Constantes de temporización ───────────────────────────────────── */
#define OS_PEND             (0xFFFFFFFFUL)   /**< Esperar indefinidamente */
#define OS_CHECK            (0x00000000UL)   /**< No bloquear             */

/** Macro para convertir ms a ticks de FreeRTOS */
#define OS_MS_TO_TICKS(ms)  pdMS_TO_TICKS(ms)

/* ── Stack dinámico (equivalente a OSAL_TASK_STACK_ALLOCATE) ───────── */
#define OSAL_TASK_STACK_ALLOCATE  NULL

/* ── Límites del sistema ────────────────────────────────────────────── */
#define OS_MAX_TASKS        16
#define OS_MAX_QUEUES       16
#define OS_MAX_MUTEXES      8
#define OS_MAX_NAME_LEN     32

/* ── Tipos base ─────────────────────────────────────────────────────── */
typedef int32_t   int32;
typedef uint32_t  uint32;
typedef uint16_t  uint16;
typedef uint8_t   uint8;
typedef size_t    osal_size_t;

/* ── Tipo de ID de objeto OSAL ──────────────────────────────────────── */
typedef uint32_t  osal_id_t;

/* ── Tipo de función de tarea ───────────────────────────────────────── */
typedef void (*osal_task_entry_t)(void);

/* Tipos adicionales de la spec OSAL v7 */
typedef uint8_t  osal_priority_t;    /* 0=más alta, 255=más baja */
typedef void     osal_stackptr_t;    /* puntero de stack (opaco) */
typedef uint32   osal_blockcount_t;  /* profundidad de cola       */


/* ── Estructura interna de tarea ────────────────────────────────────── */
typedef struct {
    TaskHandle_t    handle;
    char            name[OS_MAX_NAME_LEN];
    uint8           in_use;
} OS_task_record_t;

/* ── Estructura interna de cola ─────────────────────────────────────── */
typedef struct {
    QueueHandle_t   handle;
    char            name[OS_MAX_NAME_LEN];
    uint32          depth;
    uint32          item_size;
    uint8           in_use;
} OS_queue_record_t;

/* ── Estructura interna de mutex ────────────────────────────────────── */
typedef struct {
    SemaphoreHandle_t handle;
    char              name[OS_MAX_NAME_LEN];
    uint8             in_use;
} OS_mutex_record_t;

/* ════════════════════════════════════════════════════════════════════
 *  PROTOTIPOS DE API OSAL
 * ════════════════════════════════════════════════════════════════════ */

/**
 * @brief Inicializa el subsistema OSAL. Llamar antes de cualquier otra API.
 * @return OS_SUCCESS
 */
int32 OS_API_Init(void);

/* ── Tasks ──────────────────────────────────────────────────────────── */

/**
 * @brief Crea una tarea OSAL (mapea a xTaskCreate de FreeRTOS).
 *
 * @param[out] task_id      ID asignado a la tarea creada
 * @param[in]  task_name    Nombre de la tarea (max OS_MAX_NAME_LEN)
 * @param[in]  function_ptr Función de entrada de la tarea
 * @param[in]  stack_ptr    NULL para stack dinámico (OSAL_TASK_STACK_ALLOCATE)
 * @param[in]  stack_size   Tamaño del stack en bytes
 * @param[in]  priority     Prioridad (0 = mínima, configMAX_PRIORITIES-1 = máxima)
 * @param[in]  flags        Reservado, pasar 0
 * @return OS_SUCCESS o código de error
 */
int32 OS_TaskCreate(osal_id_t        *task_id,
                    const char       *task_name,
                    osal_task_entry_t function_ptr,
                    osal_stackptr_t  *stack_ptr,
                    uint32            stack_size,
                    osal_priority_t   priority,
                    uint32            flags);

/**
 * @brief Suspende la tarea actual por un número de milisegundos.
 * @param[in] millisecond Tiempo de espera en ms
 * @return OS_SUCCESS
 */
int32 OS_TaskDelay(uint32 millisecond);

/**
 * @brief Elimina la tarea con el ID dado.
 * @param[in] task_id ID de la tarea a eliminar
 * @return OS_SUCCESS o OS_ERROR
 */
int32 OS_TaskDelete(osal_id_t task_id);

/* ── Queues ─────────────────────────────────────────────────────────── */

/**
 * @brief Crea una cola de mensajes OSAL (mapea a xQueueCreate).
 *
 * @param[out] queue_id   ID asignado a la cola
 * @param[in]  queue_name Nombre de la cola
 * @param[in]  queue_depth Número máximo de mensajes en cola
 * @param[in]  data_size  Tamaño en bytes de cada mensaje
 * @param[in]  flags      Reservado, pasar 0
 * @return OS_SUCCESS o código de error
 */
int32 OS_QueueCreate(osal_id_t        *queue_id,
                     const char       *queue_name,
                     osal_blockcount_t queue_depth,
                     size_t            data_size,
                     uint32            flags);

/**
 * @brief Envía un mensaje a una cola OSAL (mapea a xQueueSend).
 *
 * @param[in] queue_id  ID de la cola destino
 * @param[in] data      Puntero al dato a enviar
 * @param[in] size      Tamaño del dato (debe coincidir con data_size de creación)
 * @param[in] flags     OS_CHECK (no bloquear) o tiempo en ms
 * @return OS_SUCCESS, OS_QUEUE_FULL, o OS_ERROR
 */
int32 OS_QueuePut(osal_id_t   queue_id,
                  const void *data,
                  size_t      size,
                  uint32      flags);

/**
 * @brief Recibe un mensaje de una cola OSAL (mapea a xQueueReceive).
 *
 * @param[in]  queue_id    ID de la cola fuente
 * @param[out] data        Buffer donde se copia el mensaje recibido
 * @param[in]  size        Tamaño del buffer
 * @param[out] size_copied Bytes efectivamente copiados
 * @param[in]  timeout     OS_PEND (espera indefinida), OS_CHECK, o ms
 * @return OS_SUCCESS, OS_QUEUE_EMPTY, OS_QUEUE_TIMEOUT, o OS_ERROR
 */
int32 OS_QueueGet(osal_id_t  queue_id,
                  void      *data,
                  size_t     size,
                  size_t    *size_copied,
                  int32      timeout);

/**
 * @brief Elimina una cola OSAL.
 * @param[in] queue_id ID de la cola a eliminar
 * @return OS_SUCCESS o OS_ERROR
 */
int32 OS_QueueDelete(osal_id_t queue_id);

/* ── Mutexes ────────────────────────────────────────────────────────── */

/**
 * @brief Crea un mutex OSAL (mapea a xSemaphoreCreateMutex).
 * @param[out] mutex_id   ID asignado al mutex
 * @param[in]  mutex_name Nombre del mutex
 * @return OS_SUCCESS o código de error
 */
int32 OS_MutexCreate(osal_id_t  *mutex_id,
                     const char *mutex_name,
                     uint32      options);

/**
 * @brief Toma (lock) un mutex OSAL.
 * @param[in] mutex_id ID del mutex
 * @return OS_SUCCESS o OS_ERROR
 */
int32 OS_MutexLock(osal_id_t mutex_id);

/**
 * @brief Libera (unlock) un mutex OSAL.
 * @param[in] mutex_id ID del mutex
 * @return OS_SUCCESS o OS_ERROR
 */
int32 OS_MutexUnlock(osal_id_t mutex_id);

/**
 * @brief Retorna el osal_id_t de la tarea que llama
 * @return ID de la tarea actual, o OS_MAX_TASKS si no se encuentra
 */
osal_id_t OS_TaskGetId(void);

/* ── Utilidades ─────────────────────────────────────────────────────── */

/**
 * @brief Imprime un mensaje de texto por UART3 (equivalente a printf en Linux).
 *        En el STM32H730 se redirige a HAL_UART_Transmit.
 * @param[in] fmt Formato printf
 */
void OS_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_FREERTOS_H */
