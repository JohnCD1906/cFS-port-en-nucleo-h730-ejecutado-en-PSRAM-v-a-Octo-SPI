/**
 * @file  dc_motor_app.h
 * @brief Control de motor DC — migrado a cFE Software Bus
 *
 * ── Arquitectura antes (OSAL directo) ───────────────────────────
 *
 *   SeqTask ──[OS_Queue CMD]──> CtrlTask ──[OS_Queue PWM]──> PWMTask
 *
 * ── Arquitectura después (cFE SB) ───────────────────────────────
 *
 *   SeqTask ──[SB MsgId 0x0100]──> CtrlTask ──[SB MsgId 0x0101]──> PWMTask
 *
 *   Ventaja: cualquier otra app puede suscribirse a DC_MOTOR_CMD_MID
 *   o DC_MOTOR_PWM_MID sin que SeqTask/CtrlTask lo sepan.
 *   Ejemplo futuro: app de telemetría que monitorea el estado del motor.
 */

#ifndef DC_MOTOR_APP_H
#define DC_MOTOR_APP_H

#include "osal_freertos.h"
#include "cfe_sb_stm32.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════
 * MESSAGE IDs
 *
 * Cada tipo de mensaje SB necesita un MsgId único en el sistema.
 * Convención: 0x01xx = dc_motor_app
 * ══════════════════════════════════════════════════════════════════ */

/** MsgId: SeqTask → CtrlTask (comandos de dirección/velocidad) */
#define DC_MOTOR_CMD_MID   ((CFE_SB_MsgId_t)0x0100u)

/** MsgId: CtrlTask → PWMTask (valores de PWM calculados) */
#define DC_MOTOR_PWM_MID   ((CFE_SB_MsgId_t)0x0101u)

/* ══════════════════════════════════════════════════════════════════
 * CONFIGURACIÓN DE HARDWARE
 * ══════════════════════════════════════════════════════════════════ */

#define DC_PWM_ARR          999u

/* ══════════════════════════════════════════════════════════════════
 * CONFIGURACIÓN DE CONTROL
 * ══════════════════════════════════════════════════════════════════ */

#define DC_RAMP_STEP_PCT    5u
#define DC_RAMP_DELAY_MS    50u

/* ══════════════════════════════════════════════════════════════════
 * CONFIGURACIÓN DE TAREAS Y PIPES
 * ══════════════════════════════════════════════════════════════════ */

#define DC_PWM_TASK_PRIO    1u
#define DC_CTRL_TASK_PRIO   2u
#define DC_SEQ_TASK_PRIO    3u
#define DC_TASK_STACK       4096u

#define DC_CMD_PIPE_DEPTH   4u
#define DC_PWM_PIPE_DEPTH   2u

/* ══════════════════════════════════════════════════════════════════
 * TIPOS BASE
 * ══════════════════════════════════════════════════════════════════ */

typedef enum {
    DC_DIR_COAST    = 0,
    DC_DIR_FORWARD  = 1,
    DC_DIR_BACKWARD = 2,
    DC_DIR_BRAKE    = 3
} DC_Direction_t;

/* ══════════════════════════════════════════════════════════════════
 * ESTRUCTURAS DE MENSAJE SB
 *
 * Cada mensaje SB debe empezar con CFE_MSG_Message_t (el header).
 * El payload sigue inmediatamente después.
 *
 * CFE_SB_InitMsg() inicializa el header con MsgId y tamaño total.
 * ══════════════════════════════════════════════════════════════════ */

/** Mensaje SeqTask → CtrlTask vía SB (MsgId = DC_MOTOR_CMD_MID) */
typedef struct {
    CFE_MSG_Message_t Header;      /**< SB header — siempre primero */
    DC_Direction_t    direction;   /**< Dirección objetivo */
    uint32            speed_pct;   /**< Velocidad objetivo 0-100% */
    uint32            hold_ms;     /**< Tiempo antes del siguiente cmd */
} DC_CmdMsg_t;

/** Mensaje CtrlTask → PWMTask vía SB (MsgId = DC_MOTOR_PWM_MID) */
typedef struct {
    CFE_MSG_Message_t Header;      /**< SB header — siempre primero */
    uint32            arr_ch1;     /**< TIM1_CH1: 0-DC_PWM_ARR */
    uint32            arr_ch2;     /**< TIM1_CH2: 0-DC_PWM_ARR */
} DC_PWMMsg_t;

/* ══════════════════════════════════════════════════════════════════
 * PROTOTIPOS
 * ══════════════════════════════════════════════════════════════════ */

void DC_MOTOR_AppMain(void);
void DC_SeqTask(void);
void DC_CtrlTask(void);
void DC_PWMTask(void);

#ifdef __cplusplus
}
#endif

#endif /* DC_MOTOR_APP_H */
