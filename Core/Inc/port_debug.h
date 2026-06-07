/**
 * @file  port_debug.h
 * @brief Port scaffolding debug macro for the cFS-on-PSRAM port.
 *
 * [PORT_DEBUG] messages are the porting scaffolding (PSP, OSAL,
 * filesystem bring-up). They are NOT part of cFS itself. They can be
 * silenced for a release build by commenting out PORT_DEBUG_ENABLE.
 *
 * UART prefix convention:
 *   [PORT_DEBUG]            -> port bring-up (PSP / OSAL / FS)   [toggleable]
 *   ES: / EVS: / SB:        -> cFS core services                [always on]
 *   [DC_MOTOR] / [SeqTask]  -> application                      [always on]
 *   [MX25Q] / [APS128]      -> Boot App (board project)         [external]
 */
#ifndef PORT_DEBUG_H
#define PORT_DEBUG_H

#include "osal/osal_freertos.h"

/* Comment out to silence all [PORT_DEBUG] output in a release build */
#define PORT_DEBUG_ENABLE

#ifdef PORT_DEBUG_ENABLE
  #define PORT_DBG(fmt, ...)  OS_printf("[PORT_DEBUG] " fmt, ##__VA_ARGS__)
#else
  #define PORT_DBG(fmt, ...)  ((void)0)
#endif

#endif /* PORT_DEBUG_H */
