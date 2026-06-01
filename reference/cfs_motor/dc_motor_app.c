/**
 * @file  dc_motor_app.c
 * @brief Control de motor DC — migrado a cFE Software Bus
 *
 * ── Cambios respecto a la versión OSAL directa ──────────────────
 *
 *   ANTES:  OS_QueueCreate / OS_QueuePut / OS_QueueGet
 *   AHORA:  CFE_SB_CreatePipe / CFE_SB_Subscribe / CFE_SB_TransmitMsg
 *           / CFE_SB_ReceiveBuffer
 *
 *   Las colas OSAL internas desaparecen.
 *   Los mensajes ahora viajan por el Software Bus de cFE.
 *
 * ── Ventaja del SB sobre colas directas ─────────────────────────
 *
 *   Una app de telemetría futura puede suscribirse a DC_MOTOR_CMD_MID
 *   y recibir cada comando que SeqTask envía — sin modificar SeqTask.
 *   Esto es el desacoplamiento que hace a cFS tan flexible.
 */

#include "dc_motor_app.h"
#include "cfe_es_stm32.h"
#include "cfe_evs_stm32.h"

#include "stm32f4xx_hal.h"
extern TIM_HandleTypeDef htim1;

/* ══════════════════════════════════════════════════════════════════
 * ESTADO INTERNO — ahora pipes SB en lugar de colas OSAL
 * ══════════════════════════════════════════════════════════════════ */

/** Pipe de CtrlTask — recibe comandos de SeqTask */
static CFE_SB_PipeId_t s_cmd_pipe;

/** Pipe de PWMTask — recibe valores PWM de CtrlTask */
static CFE_SB_PipeId_t s_pwm_pipe;

static osal_id_t s_seq_task_id;
static osal_id_t s_ctrl_task_id;
static osal_id_t s_pwm_task_id;

/* ══════════════════════════════════════════════════════════════════
 * HARDWARE
 * ══════════════════════════════════════════════════════════════════ */

static void hw_pwm_init(void)
{
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    OS_printf("[HW] TIM1 CH1(PE9) y CH2(PE11) iniciados @ PWM\n");
}

static void hw_pwm_set(uint32 ch1, uint32 ch2)
{
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ch1);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ch2);
    OS_printf("[HW] TIM1: CH1=%lu CH2=%lu\n",
              (unsigned long)ch1, (unsigned long)ch2);
}

/* ══════════════════════════════════════════════════════════════════
 * AUXILIAR: calcular duty cycle
 * ══════════════════════════════════════════════════════════════════ */

static void fill_pwm_msg(DC_PWMMsg_t    *msg,
                          DC_Direction_t  dir,
                          uint32          speed_pct)
{
    uint32 duty = (speed_pct * (DC_PWM_ARR + 1u)) / 100u;
    if (duty > DC_PWM_ARR) duty = DC_PWM_ARR;

    msg->arr_ch1 = 0u;
    msg->arr_ch2 = 0u;

    switch (dir)
    {
        case DC_DIR_FORWARD:
            msg->arr_ch1 = duty;
            break;
        case DC_DIR_BACKWARD:
            msg->arr_ch2 = duty;
            break;
        case DC_DIR_BRAKE:
            msg->arr_ch1 = DC_PWM_ARR;
            msg->arr_ch2 = DC_PWM_ARR;
            break;
        case DC_DIR_COAST:
        default:
            break;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * SeqTask — genera secuencia y publica por SB
 * ══════════════════════════════════════════════════════════════════ */
void DC_SeqTask(void)
{
    OS_printf("[SeqTask] Iniciada. Publicando en SB MsgId=0x%04X\n",
              (unsigned)DC_MOTOR_CMD_MID);

    static const struct {
        DC_Direction_t dir;
        uint32         speed_pct;
        uint32         hold_ms;
    } sequence[] = {
        { DC_DIR_FORWARD,  25,  3000 },
        { DC_DIR_FORWARD,  75,  3000 },
        { DC_DIR_FORWARD,  100, 2000 },
        { DC_DIR_BRAKE,    0,   1000 },
        { DC_DIR_BACKWARD, 50,  3000 },
        { DC_DIR_BACKWARD, 25,  2000 },
        { DC_DIR_COAST,    0,   1000 },
    };
    const uint32 seq_len = sizeof(sequence) / sizeof(sequence[0]);
    uint32 idx = 0u;

    DC_CmdMsg_t msg;

    while (1)
    {
        /* Inicializar mensaje SB */
        CFE_SB_InitMsg(&msg.Header,
                        DC_MOTOR_CMD_MID,
                        (uint16)sizeof(DC_CmdMsg_t));

        msg.direction = sequence[idx].dir;
        msg.speed_pct = sequence[idx].speed_pct;
        msg.hold_ms   = sequence[idx].hold_ms;

        OS_printf("[SeqTask] Paso %lu/%lu: dir=%d speed=%lu%%\n",
                  (unsigned long)(idx + 1u),
                  (unsigned long)seq_len,
                  (int)msg.direction,
                  (unsigned long)msg.speed_pct);

        /* Publicar en el SB — llega a todos los suscriptores de CMD_MID */
        CFE_Status_t ret = CFE_SB_TransmitMsg(&msg.Header, true);
        if (ret != CFE_SUCCESS && ret != CFE_SB_NO_SUBSCRIBERS)
            OS_printf("[SeqTask] WARN: TransmitMsg rc=%ld\n", (long)ret);

        OS_TaskDelay(sequence[idx].hold_ms);
        idx = (idx + 1u) % seq_len;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * CtrlTask — recibe de SB CMD_MID, publica en SB PWM_MID
 * ══════════════════════════════════════════════════════════════════ */
void DC_CtrlTask(void)
{
    OS_printf("[CtrlTask] Iniciada. Escuchando MsgId=0x%04X\n",
              (unsigned)DC_MOTOR_CMD_MID);

    uint32         current_pct = 0u;
    DC_Direction_t current_dir = DC_DIR_COAST;
    CFE_SB_Buffer_t buf;
    DC_PWMMsg_t     pwm_msg;

    while (1)
    {
        /* Esperar comando del SB */
        CFE_Status_t ret = CFE_SB_ReceiveBuffer(&buf,
                                                 s_cmd_pipe,
                                                 CFE_SB_PEND_FOREVER);
        if (ret != CFE_SUCCESS)
        {
            OS_printf("[CtrlTask] ERROR ReceiveBuffer: %ld\n", (long)ret);
            continue;
        }

        /* Castear al tipo de mensaje esperado */
        DC_CmdMsg_t *cmd = (DC_CmdMsg_t *)(void *)buf;

        OS_printf("[CtrlTask] Cmd: dir=%d target=%lu%%\n",
                  (int)cmd->direction, (unsigned long)cmd->speed_pct);

        /* Bajar a 0% antes de cambiar dirección */
        if (cmd->direction != current_dir && current_pct > 0u)
        {
            while (current_pct > 0u)
            {
                current_pct = (current_pct >= DC_RAMP_STEP_PCT)
                              ? current_pct - DC_RAMP_STEP_PCT : 0u;

                CFE_SB_InitMsg(&pwm_msg.Header,
                                DC_MOTOR_PWM_MID,
                                (uint16)sizeof(DC_PWMMsg_t));
                fill_pwm_msg(&pwm_msg, current_dir, current_pct);
                CFE_SB_TransmitMsg(&pwm_msg.Header, true);
                OS_TaskDelay(DC_RAMP_DELAY_MS);
            }
        }
        current_dir = cmd->direction;

        /* Rampa hacia el objetivo */
        uint32 target = (cmd->speed_pct > 100u) ? 100u : cmd->speed_pct;

        while (current_pct != target)
        {
            if (current_pct < target)
            {
                current_pct += DC_RAMP_STEP_PCT;
                if (current_pct > target) current_pct = target;
            }
            else
            {
                current_pct = (current_pct >= DC_RAMP_STEP_PCT)
                              ? current_pct - DC_RAMP_STEP_PCT : 0u;
                if (current_pct < target) current_pct = target;
            }

            CFE_SB_InitMsg(&pwm_msg.Header,
                            DC_MOTOR_PWM_MID,
                            (uint16)sizeof(DC_PWMMsg_t));
            fill_pwm_msg(&pwm_msg, current_dir, current_pct);
            CFE_SB_TransmitMsg(&pwm_msg.Header, true);
            OS_TaskDelay(DC_RAMP_DELAY_MS);
        }

        OS_printf("[CtrlTask] Rampa completa: dir=%d speed=%lu%%\n",
                  (int)current_dir, (unsigned long)current_pct);

        /* BRAKE y COAST necesitan comando explícito */
        if (cmd->direction == DC_DIR_BRAKE ||
            cmd->direction == DC_DIR_COAST)
        {
            CFE_SB_InitMsg(&pwm_msg.Header,
                            DC_MOTOR_PWM_MID,
                            (uint16)sizeof(DC_PWMMsg_t));
            fill_pwm_msg(&pwm_msg, cmd->direction, 0u);
            CFE_SB_TransmitMsg(&pwm_msg.Header, true);
            current_pct = 0u;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════
 * PWMTask — recibe de SB PWM_MID, escribe hardware
 * ══════════════════════════════════════════════════════════════════ */
void DC_PWMTask(void)
{
    OS_printf("[PWMTask] Iniciada. Escuchando MsgId=0x%04X\n",
              (unsigned)DC_MOTOR_PWM_MID);

    hw_pwm_init();

    CFE_SB_Buffer_t buf;

    while (1)
    {
        CFE_Status_t ret = CFE_SB_ReceiveBuffer(&buf,
                                                 s_pwm_pipe,
                                                 CFE_SB_PEND_FOREVER);
        if (ret != CFE_SUCCESS)
        {
            OS_printf("[PWMTask] ERROR ReceiveBuffer: %ld\n", (long)ret);
            continue;
        }

        DC_PWMMsg_t *pwm = (DC_PWMMsg_t *)(void *)buf;
        hw_pwm_set(pwm->arr_ch1, pwm->arr_ch2);
    }
}

/* ══════════════════════════════════════════════════════════════════
 * DC_MOTOR_AppMain
 * ══════════════════════════════════════════════════════════════════ */
void DC_MOTOR_AppMain(void)
{
    /* Dar tiempo a ES para guardar el MainTaskId */
    OS_TaskDelay(100);

    uint32_t run_status = CFE_ES_RunStatus_APP_RUN;
    CFE_ES_RegisterApp();

    OS_printf("\n[DC_MOTOR] Iniciando con cFE SB...\n");

    CFE_Status_t ret;

    /* Pipe 1: CtrlTask recibe comandos de SeqTask */
    ret = CFE_SB_CreatePipe(&s_cmd_pipe, DC_CMD_PIPE_DEPTH, "CMD_PIPE");
    if (ret != CFE_SUCCESS)
    { OS_printf("[DC_MOTOR] ERROR: CMD_PIPE\n"); return; }

    ret = CFE_SB_Subscribe(DC_MOTOR_CMD_MID, s_cmd_pipe);
    if (ret != CFE_SUCCESS)
    { OS_printf("[DC_MOTOR] ERROR: Subscribe CMD_MID\n"); return; }

    /* Pipe 2: PWMTask recibe valores PWM de CtrlTask */
    ret = CFE_SB_CreatePipe(&s_pwm_pipe, DC_PWM_PIPE_DEPTH, "PWM_PIPE");
    if (ret != CFE_SUCCESS)
    { OS_printf("[DC_MOTOR] ERROR: PWM_PIPE\n"); return; }

    ret = CFE_SB_Subscribe(DC_MOTOR_PWM_MID, s_pwm_pipe);
    if (ret != CFE_SUCCESS)
    { OS_printf("[DC_MOTOR] ERROR: Subscribe PWM_MID\n"); return; }

    /* Tareas */
    int32 oret;

    oret = OS_TaskCreate(&s_seq_task_id, "SeqTask", DC_SeqTask,
                         OSAL_TASK_STACK_ALLOCATE,
                         DC_TASK_STACK, DC_SEQ_TASK_PRIO, 0);
    if (oret != OS_SUCCESS)
    { OS_printf("[DC_MOTOR] ERROR: SeqTask\n"); return; }

    oret = OS_TaskCreate(&s_ctrl_task_id, "CtrlTask", DC_CtrlTask,
                         OSAL_TASK_STACK_ALLOCATE,
                         DC_TASK_STACK, DC_CTRL_TASK_PRIO, 0);
    if (oret != OS_SUCCESS)
    { OS_printf("[DC_MOTOR] ERROR: CtrlTask\n"); return; }

    oret = OS_TaskCreate(&s_pwm_task_id, "PWMTask", DC_PWMTask,
                         OSAL_TASK_STACK_ALLOCATE,
                         DC_TASK_STACK, DC_PWM_TASK_PRIO, 0);
    if (oret != OS_SUCCESS)
    { OS_printf("[DC_MOTOR] ERROR: PWMTask\n"); return; }

    OS_printf("[DC_MOTOR] SB pipes y tareas listas.\n");

    while (CFE_ES_RunLoop(&run_status))
    {
        OS_TaskDelay(1000);
    }

    CFE_ES_ExitApp(run_status);
}
