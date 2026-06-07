/**
 * @file    osal_freertos.c
 * @brief   Port OSAL → FreeRTOS para STM32H730IBT6Q
 *
 * ╔══════════════════════════════════════════════════════════════════╗
 * ║  COMO LEER ESTE ARCHIVO                                         ║
 * ║                                                                 ║
 * ║  Cada función OSAL_xxx() hace DOS cosas:                        ║
 * ║    1. Lógica propia (validar parámetros, gestionar tabla)       ║
 * ║    2. Llamar a FreeRTOS — marcado con comentario [FREERTOS]     ║
 * ║                                                                 ║
 * ║  Busca [FREERTOS] en este archivo para ver todos los puntos     ║
 * ║  donde OSAL le habla directamente a FreeRTOS.                   ║
 * ╚══════════════════════════════════════════════════════════════════╝
 *
 * BUGS CORREGIDOS vs versión anterior:
 *   BUG1: OS_QueuePut — flags NO es timeout (spec dice flags=0 siempre)
 *   BUG2: Prioridades OSAL invertidas (OSAL 0=más alta, 255=más baja)
 *   BUG3: OS_QueueGet — distingue OS_QUEUE_EMPTY vs OS_QUEUE_TIMEOUT
 *
 * @target  STM32H730IBT6Q — Cortex-M7 @ 550 MHz
 * @rtos    FreeRTOS v10.x
 */

//#include "osal_freertos_fixed.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "osal/osal_freertos.h"
#include "libs/uart_debug.h"   /* tu uart_printf */
#include "port_debug.h"   /* PORT_DBG macro */
/*
 * [FREERTOS] En STM32, los headers de FreeRTOS vienen de CubeMX.
 * En simulación Linux los usamos como stubs.
 * FreeRTOS expone su API pública a través de estos headers:
 *   FreeRTOS.h  → tipos base (TickType_t, BaseType_t, UBaseType_t...)
 *   task.h      → xTaskCreate, vTaskDelete, vTaskDelay
 *   queue.h     → xQueueCreate, xQueueSend, xQueueReceive, vQueueDelete
 *   semphr.h    → xSemaphoreCreateMutex, xSemaphoreTake, xSemaphoreGive
 */
#include "FreeRTOS.h"  /* [FREERTOS] tipos base */
#include "task.h"      /* [FREERTOS] API de tareas */
#include "queue.h"     /* [FREERTOS] API de colas */
#include "semphr.h"    /* [FREERTOS] API de semáforos/mutex */

/* ── En STM32, incluir el HAL para UART ─────────────────────────── */
//#include "stm32f4xx_hal.h"
//extern UART_HandleTypeDef huart3;

/* ══════════════════════════════════════════════════════════════════
 * TABLAS INTERNAS DE OBJETOS OSAL
 *
 * OSAL mantiene sus propias tablas de control. Cada entrada en la
 * tabla guarda el handle de FreeRTOS del objeto y metadatos.
 *
 * Por qué tablas propias y no usar directamente los handles de FreeRTOS:
 *   - OSAL identifica sus objetos con osal_id_t (un entero simple)
 *   - FreeRTOS identifica sus objetos con handles opacos (punteros)
 *   - Las tablas son el puente entre ambos mundos
 * ══════════════════════════════════════════════════════════════════ */
static OS_task_record_t  OS_task_table[OS_MAX_TASKS];
static OS_queue_record_t OS_queue_table[OS_MAX_QUEUES];
static OS_mutex_record_t OS_mutex_table[OS_MAX_MUTEXES];

/*
 * Mutex interno de OSAL para proteger las tablas de acceso concurrente.
 * [FREERTOS] Es un SemaphoreHandle_t — el tipo de handle de semáforo de FreeRTOS.
 */
static SemaphoreHandle_t OS_table_mutex = NULL; /* [FREERTOS] handle */

/*
 * Mutex dedicado para serializar OS_printf entre tareas.
 * Sin esto, los OS_printf de varias tareas se entremezclan caracter
 * por caracter en el UART (logs ilegibles).
 *
 * El flag s_uart_ready arranca en 0: antes de OS_API_Init el mutex no
 * existe, asi que OS_printf imprime sin lock (single-thread, pre-scheduler).
 * Tras crear el mutex, s_uart_ready=1 y OS_printf se serializa.
 */
static SemaphoreHandle_t OS_uart_mutex = NULL;
static volatile uint8    s_uart_ready  = 0;

/*
 * [FREERTOS] xSemaphoreTake / xSemaphoreGive son las funciones de FreeRTOS
 * para tomar y liberar un semáforo/mutex.
 * portMAX_DELAY = esperar indefinidamente (valor especial de FreeRTOS).
 */
#define OS_LOCK_TABLES()   xSemaphoreTake(OS_table_mutex, portMAX_DELAY)
#define OS_UNLOCK_TABLES() xSemaphoreGive(OS_table_mutex)

/* ══════════════════════════════════════════════════════════════════
 * OS_API_Init — Inicialización de OSAL
 *
 * Spec: "Must be called before any other OS routine."
 * Spec: "Not intended for user application use — typically BSP/PSP only."
 * ══════════════════════════════════════════════════════════════════ */
int32 OS_API_Init(void)
{
    memset(OS_task_table,  0, sizeof(OS_task_table));
    memset(OS_queue_table, 0, sizeof(OS_queue_table));
    memset(OS_mutex_table, 0, sizeof(OS_mutex_table));

    /*
     * [FREERTOS] xSemaphoreCreateMutex() crea un mutex con herencia de
     * prioridad. Retorna NULL si no hay heap suficiente.
     * Este mutex protege las tablas internas de OSAL de acceso
     * concurrente entre tareas FreeRTOS.
     */
    OS_table_mutex = xSemaphoreCreateMutex(); /* [FREERTOS] */
    if (OS_table_mutex == NULL)
    {
        /* Sin heap suficiente para el mutex interno */
        return OS_ERROR;
    }

    OS_uart_mutex = xSemaphoreCreateMutex();
        if (OS_uart_mutex == NULL)
        {
            return OS_ERROR;
        }
        s_uart_ready = 1;

        PORT_DBG("OSAL initialized on FreeRTOS\n");
        PORT_DBG("OSAL: max tasks=%d, queues=%d, mutexes=%d\n",
                 OS_MAX_TASKS, OS_MAX_QUEUES, OS_MAX_MUTEXES);
        return OS_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════
 * TAREAS
 *
 * PROBLEMA DE FIRMA:
 *   OSAL define: typedef void (*osal_task_entry_t)(void)
 *                → la función de la tarea NO recibe parámetros
 *
 *   FreeRTOS exige: typedef void (*TaskFunction_t)(void *pvParameters)
 *                → la función de la tarea DEBE recibir void*
 *
 * SOLUCIÓN: OS_TaskWrapper — función intermediaria con la firma que
 * exige FreeRTOS, que internamente llama a la función OSAL sin params.
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    osal_task_entry_t entry; /* función OSAL del usuario */
} OS_task_wrapper_t;

/*
 * [FREERTOS] Esta función tiene la firma exacta que exige FreeRTOS
 * para TaskFunction_t: void f(void *param).
 * FreeRTOS llama a esta función cuando arranca la tarea.
 * Nosotros la usamos como puente para llamar a la función OSAL.
 */
static void OS_TaskWrapper(void *param) /* [FREERTOS] firma obligatoria */
{
    OS_task_wrapper_t *w = (OS_task_wrapper_t *)param;
    if (w && w->entry)
    {
        w->entry(); /* llama a la función OSAL: void f(void) */
    }
    /*
     * [FREERTOS] vTaskDelete(NULL) elimina la tarea actual.
     * Se llama si la función de la tarea retorna (no debería en
     * una tarea normal con while(1), pero es necesario como seguridad).
     */
    vTaskDelete(NULL); /* [FREERTOS] */
}

/* ──────────────────────────────────────────────────────────────────
 * OS_TaskCreate
 *
 * CORRECCIÓN BUG2: Prioridades
 *   La guía OSAL dice:
 *     0   = prioridad MÁS ALTA (preempta a todas)
 *     255 = prioridad MÁS BAJA (no preempta a nadie)
 *
 *   FreeRTOS dice lo OPUESTO:
 *     0                      = prioridad MÁS BAJA
 *     configMAX_PRIORITIES-1 = prioridad MÁS ALTA
 *
 *   Conversión correcta:
 *     freertos_prio = (configMAX_PRIORITIES - 1) - escalar(osal_prio)
 * ────────────────────────────────────────────────────────────────── */
int32 OS_TaskCreate(osal_id_t         *task_id,
                    const char        *task_name,
                    osal_task_entry_t  function_ptr,
                    osal_stackptr_t   *stack_ptr,
                    uint32             stack_size,
                    osal_priority_t    priority,
                    uint32             flags)
{
    (void)stack_ptr; /* Spec: NULL → FreeRTOS asigna stack del heap */
    (void)flags;     /* Reservado, ignorar */

    if (task_id == NULL || task_name == NULL || function_ptr == NULL)
        return OS_INVALID_POINTER;
    if (stack_size == 0)
        return OS_ERR_INVALID_SIZE;

    /* Buscar slot libre en tabla OSAL */
    OS_LOCK_TABLES();
    int32 slot = -1;
    for (int i = 0; i < OS_MAX_TASKS; i++)
    {
        if (!OS_task_table[i].in_use) { slot = i; break; }
    }
    OS_UNLOCK_TABLES();

    if (slot < 0)
        return OS_ERR_NO_FREE_IDS;

    /* Guardar entry point para el wrapper */
    static OS_task_wrapper_t wrappers[OS_MAX_TASKS];
    wrappers[slot].entry = function_ptr;

    /*
     * Convertir stack de bytes → words.
     * [FREERTOS] xTaskCreate recibe el stack en words (StackType_t = uint32
     * en Cortex-M7). La spec OSAL habla de bytes, FreeRTOS de words.
     */
    uint32 stack_words = stack_size / sizeof(StackType_t); /* [FREERTOS] StackType_t */
    if (stack_words < (uint32)configMINIMAL_STACK_SIZE)    /* [FREERTOS] configMINIMAL_STACK_SIZE */
        stack_words = (uint32)configMINIMAL_STACK_SIZE;

    /*
     * ── CORRECCIÓN BUG2: mapeo de prioridad OSAL → FreeRTOS ──────
     *
     * OSAL:     0 = más alta ──────────────────────────────────► 255 = más baja
     * FreeRTOS: 0 = más baja ◄──────────────────────────────────  configMAX_PRIORITIES-1
     *
     * Fórmula: freertos = (MAX-1) - round(osal_prio * (MAX-1) / 255)
     *
     * Ejemplos con configMAX_PRIORITIES=8 (rango FreeRTOS: 0-7):
     *   OSAL  0  → FreeRTOS 7  (más alta)
     *   OSAL  1  → FreeRTOS 7  (≈más alta, escala pequeña)
     *   OSAL 64  → FreeRTOS 5
     *   OSAL128  → FreeRTOS 4
     *   OSAL255  → FreeRTOS 0  (más baja)
     *
     * [FREERTOS] UBaseType_t es el tipo de prioridad de FreeRTOS.
     */
    uint32 max_prio = (uint32)configMAX_PRIORITIES - 1u; /* [FREERTOS] */
    UBaseType_t freertos_prio =                          /* [FREERTOS] UBaseType_t */
        (UBaseType_t)(max_prio - (priority * max_prio / 255u));

    /*
     * [FREERTOS] xTaskCreate — la función principal de creación de tareas.
     *
     * Parámetros:
     *   pvTaskCode    = OS_TaskWrapper (puente firma OSAL→FreeRTOS)
     *   pcName        = nombre de la tarea (para debug con FreeRTOS)
     *   usStackDepth  = tamaño del stack en WORDS (no bytes)
     *   pvParameters  = puntero al wrapper con el entry point OSAL
     *   uxPriority    = prioridad en escala FreeRTOS (0=baja, MAX-1=alta)
     *   pxCreatedTask = handle de salida (para poder borrar/suspender luego)
     *
     * Retorna: pdPASS si OK, pdFAIL si no hay heap suficiente.
     *
     * IMPORTANTE: xTaskCreate registra la tarea en el scheduler de FreeRTOS
     * pero NO la ejecuta todavía. La tarea empieza a correr cuando
     * vTaskStartScheduler() es llamado desde main().
     */
    TaskHandle_t handle = NULL;
    BaseType_t result = xTaskCreate(  /* [FREERTOS] ←── aquí FreeRTOS crea la tarea */
        OS_TaskWrapper,               /* función con firma FreeRTOS */
        task_name,                    /* nombre para debug */
        (uint16_t)stack_words,        /* stack en words */
        &wrappers[slot],              /* parámetro → nuestro wrapper */
        freertos_prio,                /* prioridad en escala FreeRTOS */
        &handle                       /* handle de salida */
    );

    if (result != pdPASS || handle == NULL)
        {
            OS_printf("OSAL ERROR: xTaskCreate failed for '%s' "
                      "(out of heap or invalid priority)\n", task_name);
            return OS_ERROR;
        }

    /* Registrar en tabla OSAL */
    OS_LOCK_TABLES();
    OS_task_table[slot].handle = handle; /* guardamos el handle FreeRTOS */
    strncpy(OS_task_table[slot].name, task_name, OS_MAX_NAME_LEN - 1);
    OS_task_table[slot].in_use = 1;
    OS_UNLOCK_TABLES();

    *task_id = (osal_id_t)slot;

    PORT_DBG("Task '%s' OK "
             "(slot=%d, osal_prio=%u->freertos_prio=%u, stack=%u words)\n",
             task_name, (int)slot,
             (unsigned)priority, (unsigned)freertos_prio,
             (unsigned)stack_words);
    return OS_SUCCESS;
}

/* ──────────────────────────────────────────────────────────────────
 * OS_TaskDelay
 *
 * Spec: "Scheduled wait — NOT a busy wait."
 * ────────────────────────────────────────────────────────────────── */
int32 OS_TaskDelay(uint32 millisecond)
{
    /*
     * [FREERTOS] vTaskDelay suspende la tarea actual por N ticks.
     *
     * pdMS_TO_TICKS convierte ms → ticks según configTICK_RATE_HZ.
     * Con configTICK_RATE_HZ=1000: 1 tick = 1 ms.
     *
     * Mientras esta tarea duerme, FreeRTOS ejecuta otras tareas.
     * Esta es la cooperación implícita con el scheduler de FreeRTOS.
     */
    vTaskDelay(pdMS_TO_TICKS(millisecond)); /* [FREERTOS] */
    return OS_SUCCESS;
}

int32 OS_TaskDelete(osal_id_t task_id)
{
    if (task_id >= OS_MAX_TASKS || !OS_task_table[task_id].in_use)
        return OS_ERR_INVALID_ID;

    OS_LOCK_TABLES();
    vTaskDelete(OS_task_table[task_id].handle); /* [FREERTOS] */
    memset(&OS_task_table[task_id], 0, sizeof(OS_task_record_t));
    OS_UNLOCK_TABLES();

    return OS_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════
 * COLAS DE MENSAJES
 *
 * Las colas OSAL son FIFO de mensajes de tamaño fijo.
 * Cada mensaje es una copia completa del dato (no un puntero).
 * FreeRTOS implementa exactamente esto con xQueueCreate.
 * ══════════════════════════════════════════════════════════════════ */

int32 OS_QueueCreate(osal_id_t        *queue_id,
                     const char       *queue_name,
                     osal_blockcount_t queue_depth,
                     size_t            data_size,
                     uint32            flags)
{
    (void)flags; /* Reservado según spec, siempre 0 */

    if (queue_id == NULL || queue_name == NULL)
        return OS_INVALID_POINTER;
    if (data_size == 0)
        return OS_ERR_INVALID_SIZE;

    OS_LOCK_TABLES();
    int32 slot = -1;
    for (int i = 0; i < OS_MAX_QUEUES; i++)
    {
        if (!OS_queue_table[i].in_use) { slot = i; break; }
    }
    OS_UNLOCK_TABLES();

    if (slot < 0)
        return OS_ERR_NO_FREE_IDS;

    /*
     * [FREERTOS] xQueueCreate crea una cola FIFO.
     *
     * Parámetros:
     *   uxQueueLength = profundidad (cuántos mensajes caben)
     *   uxItemSize    = tamaño de cada mensaje en bytes
     *
     * FreeRTOS asigna internamente:
     *   heap_usado = uxQueueLength * uxItemSize + overhead_interno
     *
     * Retorna NULL si no hay heap suficiente.
     */
    QueueHandle_t handle = xQueueCreate( /* [FREERTOS] ←── FreeRTOS crea la cola */
        (UBaseType_t)queue_depth,        /* profundidad */
        (UBaseType_t)data_size           /* tamaño de cada ítem en bytes */
    );
    if (handle == NULL)
        {
            OS_printf("OSAL ERROR: xQueueCreate failed for '%s'\n", queue_name);
            return OS_ERROR;
        }

    OS_LOCK_TABLES();
    OS_queue_table[slot].handle    = handle;
    OS_queue_table[slot].depth     = (uint32)queue_depth;
    OS_queue_table[slot].item_size = (uint32)data_size;
    strncpy(OS_queue_table[slot].name, queue_name, OS_MAX_NAME_LEN - 1);
    OS_queue_table[slot].in_use    = 1;
    OS_UNLOCK_TABLES();

    *queue_id = (osal_id_t)slot;

    PORT_DBG("Queue '%s' OK (slot=%d, depth=%u, item=%u bytes)\n",
                 queue_name, (int)slot,
                 (unsigned)queue_depth, (unsigned)data_size);
        return OS_SUCCESS;
}

/* ──────────────────────────────────────────────────────────────────
 * OS_QueuePut — CORRECCIÓN BUG1
 *
 * ANTES (incorrecto):
 *   Tratábamos 'flags' como si fuera un timeout en ms.
 *   Pasábamos pdMS_TO_TICKS(flags) a xQueueSend.
 *
 * AHORA (correcto según spec):
 *   flags = "Currently reserved/unused, should be passed as 0"
 *   OS_QueuePut NUNCA bloquea. Si la cola está llena → OS_QUEUE_FULL.
 *   El timeout=0 en xQueueSend significa "no esperar" (non-blocking).
 * ────────────────────────────────────────────────────────────────── */
int32 OS_QueuePut(osal_id_t   queue_id,
                  const void *data,
                  size_t      size,
                  uint32      flags)
{
    (void)size;  /* FreeRTOS usa el item_size fijo registrado al crear */
    (void)flags; /* Spec: reservado, ignorar — NO es un timeout */

    if (queue_id >= OS_MAX_QUEUES || !OS_queue_table[queue_id].in_use)
        return OS_ERR_INVALID_ID;
    if (data == NULL)
        return OS_INVALID_POINTER;

    /*
     * [FREERTOS] xQueueSend copia el mensaje al final de la cola (FIFO).
     *
     * Parámetros:
     *   xQueue       = handle de la cola FreeRTOS
     *   pvItemToQueue = puntero al dato a copiar
     *   xTicksToWait  = tiempo máximo a esperar si la cola está llena
     *
     * CORRECCIÓN: siempre pasamos 0 (no esperar).
     * Si la cola está llena, retorna pdFAIL inmediatamente.
     * El caller debe decidir qué hacer con OS_QUEUE_FULL.
     */
    BaseType_t result = xQueueSend(       /* [FREERTOS] ←── FreeRTOS encola el dato */
        OS_queue_table[queue_id].handle,
        data,
        0                                 /* timeout=0: no bloquear nunca */
    );

    if (result != pdPASS) /* [FREERTOS] pdPASS = 1, pdFAIL = 0 */
        return OS_QUEUE_FULL;

    return OS_SUCCESS;
}

/* ──────────────────────────────────────────────────────────────────
 * OS_QueueGet — CORRECCIÓN BUG3
 *
 * La spec define tres modos para el parámetro timeout:
 *   OS_PEND  (-1): bloquear indefinidamente hasta recibir mensaje
 *   OS_CHECK  (0): no bloquear; retornar OS_QUEUE_EMPTY si vacía
 *   N > 0       : bloquear hasta N ms; retornar OS_QUEUE_TIMEOUT
 *
 * FreeRTOS tiene el mismo concepto con xQueueReceive:
 *   portMAX_DELAY → esperar indefinidamente
 *   0             → no esperar
 *   pdMS_TO_TICKS(N) → esperar N ms
 *
 * La distinción OS_QUEUE_EMPTY vs OS_QUEUE_TIMEOUT es semántica:
 *   EMPTY   = "no había nada" (CHECK inmediato)
 *   TIMEOUT = "esperé y no llegó nada" (timeout expiró)
 * ────────────────────────────────────────────────────────────────── */
int32 OS_QueueGet(osal_id_t  queue_id,
                  void      *data,
                  size_t     size,
                  size_t    *size_copied,
                  int32      timeout)
{
    (void)size; /* FreeRTOS usa el item_size fijo de creación */

    if (queue_id >= OS_MAX_QUEUES || !OS_queue_table[queue_id].in_use)
        return OS_ERR_INVALID_ID;
    if (data == NULL || size_copied == NULL)
        return OS_INVALID_POINTER;

    /*
     * Traducir timeout OSAL → ticks FreeRTOS
     *
     * [FREERTOS] TickType_t es el tipo de ticks de FreeRTOS.
     *            portMAX_DELAY = 0xFFFFFFFF = esperar para siempre.
     *            pdMS_TO_TICKS convierte ms a ticks según configTICK_RATE_HZ.
     */
    TickType_t ticks; /* [FREERTOS] TickType_t */
    if (timeout == OS_PEND)        /* -1: esperar siempre */
        ticks = portMAX_DELAY;     /* [FREERTOS] portMAX_DELAY */
    else if (timeout == OS_CHECK)  /* 0: no esperar */
        ticks = 0;
    else                           /* N ms: esperar N ms */
        ticks = pdMS_TO_TICKS((uint32)timeout); /* [FREERTOS] pdMS_TO_TICKS */

    /*
     * [FREERTOS] xQueueReceive desencola el primer mensaje (FIFO).
     *
     * Si hay un mensaje disponible: copia el dato a 'data', retorna pdPASS.
     * Si no hay mensaje y ticks=0: retorna pdFAIL inmediatamente.
     * Si no hay mensaje y ticks>0: suspende la tarea hasta que:
     *   a) llegue un mensaje → retorna pdPASS
     *   b) se agote el timeout → retorna pdFAIL
     *
     * Cuando la tarea está suspendida aquí, FreeRTOS ejecuta otras tareas.
     * Este es el mecanismo de bloqueo/desbloqueo entre tareas.
     */
    BaseType_t result = xQueueReceive(     /* [FREERTOS] ←── FreeRTOS desencola */
        OS_queue_table[queue_id].handle,
        data,
        ticks
    );

    if (result == pdPASS) /* [FREERTOS] pdPASS */
    {
        *size_copied = OS_queue_table[queue_id].item_size;
        return OS_SUCCESS;
    }

    /* CORRECCIÓN BUG3: distinguir el motivo del fallo */
    if (timeout == OS_CHECK)
        return OS_QUEUE_EMPTY;    /* no había nada, no esperamos */
    else
        return OS_QUEUE_TIMEOUT;  /* esperamos y se acabó el tiempo */
}

int32 OS_QueueDelete(osal_id_t queue_id)
{
    if (queue_id >= OS_MAX_QUEUES || !OS_queue_table[queue_id].in_use)
        return OS_ERR_INVALID_ID;

    OS_LOCK_TABLES();
    vQueueDelete(OS_queue_table[queue_id].handle); /* [FREERTOS] */
    memset(&OS_queue_table[queue_id], 0, sizeof(OS_queue_record_t));
    OS_UNLOCK_TABLES();

    return OS_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════════
 * MUTEXES
 *
 * OSAL mutex = FreeRTOS mutex con herencia de prioridad.
 * La herencia de prioridad evita inversión de prioridad:
 *   Si tarea de alta prioridad espera un mutex que tiene una tarea
 *   de baja prioridad, FreeRTOS eleva temporalmente la prioridad
 *   de la tarea baja para que libere el mutex rápido.
 * ══════════════════════════════════════════════════════════════════ */

int32 OS_MutexCreate(osal_id_t  *mutex_id,
                     const char *mutex_name,
                     uint32      options)
{
    (void)options;

    if (mutex_id == NULL || mutex_name == NULL)
        return OS_INVALID_POINTER;

    OS_LOCK_TABLES();
    int32 slot = -1;
    for (int i = 0; i < OS_MAX_MUTEXES; i++)
    {
        if (!OS_mutex_table[i].in_use) { slot = i; break; }
    }
    OS_UNLOCK_TABLES();

    if (slot < 0)
        return OS_ERR_NO_FREE_IDS;

    /*
     * [FREERTOS] xSemaphoreCreateMutex crea un mutex con herencia
     * de prioridad (Priority Inheritance Mutex).
     * Retorna NULL si no hay heap disponible.
     */
    SemaphoreHandle_t handle = xSemaphoreCreateMutex(); /* [FREERTOS] */
    if (handle == NULL)
        return OS_ERROR;

    OS_LOCK_TABLES();
    OS_mutex_table[slot].handle = handle;
    strncpy(OS_mutex_table[slot].name, mutex_name, OS_MAX_NAME_LEN - 1);
    OS_mutex_table[slot].in_use = 1;
    OS_UNLOCK_TABLES();

    *mutex_id = (osal_id_t)slot;
        PORT_DBG("Mutex '%s' OK (slot=%d)\n", mutex_name, (int)slot);
        return OS_SUCCESS;
}

int32 OS_MutexLock(osal_id_t mutex_id)
{
    if (mutex_id >= OS_MAX_MUTEXES || !OS_mutex_table[mutex_id].in_use)
        return OS_ERR_INVALID_ID;

    /*
     * [FREERTOS] xSemaphoreTake intenta tomar el mutex.
     * Con portMAX_DELAY: bloquea hasta obtenerlo.
     * Si otra tarea lo tiene, esta tarea queda suspendida en FreeRTOS
     * hasta que la otra tarea llame xSemaphoreGive.
     */
    BaseType_t r = xSemaphoreTake(         /* [FREERTOS] */
        OS_mutex_table[mutex_id].handle,
        portMAX_DELAY                      /* [FREERTOS] esperar siempre */
    );
    return (r == pdPASS) ? OS_SUCCESS : OS_ERROR;
}

int32 OS_MutexUnlock(osal_id_t mutex_id)
{
    if (mutex_id >= OS_MAX_MUTEXES || !OS_mutex_table[mutex_id].in_use)
        return OS_ERR_INVALID_ID;

    /*
     * [FREERTOS] xSemaphoreGive libera el mutex.
     * Si hay tareas bloqueadas esperándolo, FreeRTOS despierta
     * la de mayor prioridad.
     */
    BaseType_t r = xSemaphoreGive(OS_mutex_table[mutex_id].handle); /* [FREERTOS] */
    return (r == pdPASS) ? OS_SUCCESS : OS_ERROR;
}


osal_id_t OS_TaskGetId(void)
{
    TaskHandle_t cur = xTaskGetCurrentTaskHandle(); /* [FREERTOS] */
    for (uint32_t i = 0; i < OS_MAX_TASKS; i++)
    {
        if (OS_task_table[i].in_use &&
            OS_task_table[i].handle == cur)
        {
            return (osal_id_t)i;
        }
    }
    return (osal_id_t)OS_MAX_TASKS;  /* no encontrado */
}

/* ══════════════════════════════════════════════════════════════════

 * ══════════════════════════════════════════════════════════════════ */
void OS_printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    /*
     * Serializar el acceso al UART solo si:
     *   1. El mutex ya existe (s_uart_ready), y
     *   2. El scheduler esta corriendo (si no, xSemaphoreTake no debe
     *      llamarse — mismo problema que vTaskDelay pre-scheduler).
     *
     * xTaskGetSchedulerState() devuelve taskSCHEDULER_RUNNING solo
     * despues de osKernelStart(). Antes, imprimimos sin lock (es
     * single-thread de todas formas).
     */
    if (s_uart_ready &&
        xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        xSemaphoreTake(OS_uart_mutex, portMAX_DELAY);
        uart_puts(buf);
        xSemaphoreGive(OS_uart_mutex);
    }
    else
    {
        uart_puts(buf);
    }
}
