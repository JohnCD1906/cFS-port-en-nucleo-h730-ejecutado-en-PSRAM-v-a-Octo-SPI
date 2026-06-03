/**
 * @file  dc_motor_app.h
 * @brief Control de motor DC — migrado a cFE Software Bus
 *
 * Arquitectura con cFE SB:
 *   SeqTask --[SB MsgId 0x0100]--> CtrlTask --[SB MsgId 0x0101]--> PWMTask
 */

#ifndef DC_MOTOR_APP_H
#define DC_MOTOR_APP_H

#include "osal/osal_freertos.h"
#include "cfe/cfe_sb_stm32.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Message IDs (0x01xx = dc_motor_app) ─────────────────────────── */
#define DC_MOTOR_CMD_MID   ((CFE_SB_MsgId_t)0x0100u)
#define DC_MOTOR_PWM_MID   ((CFE_SB_MsgId_t)0x0101u)

/* ── Hardware ────────────────────────────────────────────────────── */
#define DC_PWM_ARR          999u

/* ── Control ─────────────────────────────────────────────────────── */
#define DC_RAMP_STEP_PCT    5u
#define DC_RAMP_DELAY_MS    50u

/* ── Tareas y pipes ──────────────────────────────────────────────── */
#define DC_PWM_TASK_PRIO    1u
#define DC_CTRL_TASK_PRIO   2u
#define DC_SEQ_TASK_PRIO    3u
#define DC_TASK_STACK       4096u

#define DC_CMD_PIPE_DEPTH   4u
#define DC_PWM_PIPE_DEPTH   2u

/* ── Tipos base ──────────────────────────────────────────────────── */
typedef enum {
    DC_DIR_COAST    = 0,
    DC_DIR_FORWARD  = 1,
    DC_DIR_BACKWARD = 2,
    DC_DIR_BRAKE    = 3
} DC_Direction_t;

/* ── Estructuras de mensaje SB ───────────────────────────────────── */
typedef struct {
    CFE_MSG_Message_t Header;
    DC_Direction_t    direction;
    uint32            speed_pct;
    uint32            hold_ms;
} DC_CmdMsg_t;

typedef struct {
    CFE_MSG_Message_t Header;
    uint32            arr_ch1;
    uint32            arr_ch2;
} DC_PWMMsg_t;

/* ── Prototipos ──────────────────────────────────────────────────── */
void DC_MOTOR_AppMain(void);
void DC_SeqTask(void);
void DC_CtrlTask(void);
void DC_PWMTask(void);

#ifdef __cplusplus
}
#endif

#endif /* DC_MOTOR_APP_H */
