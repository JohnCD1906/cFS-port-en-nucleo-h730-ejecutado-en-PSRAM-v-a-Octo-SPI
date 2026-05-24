/**
 * @file  cfe_es_platform_cfg.h
 * @brief Parámetros de configuración de cFE ES para STM32
 *
 * Adaptado de NASA cFS Draco — sección platform_cfg.
 * Valores reducidos para caber en el STM32F439ZI (256 KB SRAM)
 * y STM32H730VBT6 (288 KB SRAM distribuida).
 *
 * Referencia original:
 *   cfe/modules/es/config/default_cfe_es_internal_cfg_values.h
 */
#ifndef CFE_ES_PLATFORM_CFG_H
#define CFE_ES_PLATFORM_CFG_H
/* ══════════════════════════════════════════════════════════════════
 * TABLA DE APLICACIONES
 *
══════════════════════════════════════════════════════════════════ */
/** Máximo de apps externas registradas simultáneamente */
#define CFE_PLATFORM_ES_MAX_APPLICATIONS    8u
/** Máximo de librerías cargadas simultáneamente (no usadas aún) */
#define CFE_PLATFORM_ES_MAX_LIBRARIES       4u
/** Máximo de contadores genéricos (no usados aún) */
#define CFE_PLATFORM_ES_MAX_GEN_COUNTERS    4u
/** Máximo de memory pools */
#define CFE_PLATFORM_ES_MAX_MEMORY_POOLS    4u
/* ══════════════════════════════════════════════════════════════════
 * SYSTEM LOG — buffer circular en el área de reset
 *
══════════════════════════════════════════════════════════════════ */
/** Tamaño en bytes del System Log (en el área de reset del PSP) */
#define CFE_PLATFORM_ES_SYSTEM_LOG_SIZE     512u
/** Modo por defecto del syslog en Power-On Reset: OVERWRITE */
#define CFE_PLATFORM_ES_DEFAULT_POR_SYSLOG_MODE   0u  /* 0 = sobreescribir */
/** Modo por defecto del syslog en Processor Reset: DISCARD */
#define CFE_PLATFORM_ES_DEFAULT_PR_SYSLOG_MODE    1u  /* 1 = descartar nuevos */
/* ══════════════════════════════════════════════════════════════════
 * STARTUP SCRIPT
 *
══════════════════════════════════════════════════════════════════ */
/** Path al startup script en el RAM disk — usado por PSP_FS_Init */
#define CFE_PLATFORM_ES_NONVOL_STARTUP_FILE   "/cf/startup.scr" //revisar porque creo que le cambie en nombre
/** Path al startup script volátil (no implementado en STM32) */
#define CFE_PLATFORM_ES_VOLATILE_STARTUP_FILE "/cf/vol_startup.scr"
/** Timeout en ms esperando que las apps terminen de inicializar */
#define CFE_PLATFORM_ES_STARTUP_SCRIPT_TIMEOUT_MSEC   5000u
/** Período de polling del sync de startup en ms */
#define CFE_PLATFORM_ES_STARTUP_SYNC_POLL_MSEC        100u
/** Máximo de tokens por línea del startup script */
#define CFE_ES_STARTSCRIPT_MAX_TOKENS_PER_LINE        8u
/* ══════════════════════════════════════════════════════════════════
 * STACK POR DEFECTO
 *
══════════════════════════════════════════════════════════════════ */
/** Stack por defecto para apps si no se especifica en el script (bytes) */
#define CFE_PLATFORM_ES_DEFAULT_STACK_SIZE    2048u
/* ══════════════════════════════════════════════════════════════════
 * EXCEPTION / RESET LOG
 *
══════════════════════════════════════════════════════════════════ */
/** Máximo de entradas en el ER Log (en el área de reset del PSP) */
#define CFE_PLATFORM_ES_ER_LOG_ENTRIES        4u
/** Máximo de resets de procesador antes de escalar a Power-On */
#define CFE_PLATFORM_ES_MAX_PROCESSOR_RESETS  3u
/* ══════════════════════════════════════════════════════════════════
 * STARTUP CORE APPS — tiempo máximo de startup para core apps
 *
══════════════════════════════════════════════════════════════════ */
#define CFE_PLATFORM_CORE_MAX_STARTUP_MSEC    2000u
#endif /* CFE_ES_PLATFORM_CFG_H */
