#include "osal/osal_freertos.h"
#include "libs/uart_debug.h"

static void osal_test_entry(void)   /* firma OSAL: void f(void) */
{
    uint32_t tick = 0;
    OS_printf("[osal_test] Tarea OSAL arrancada\r\n");
    while (1) {
        OS_printf("[osal_test] tick=%lu\r\n", (unsigned long)tick++);
        OS_TaskDelay(1000);   /* 1 segundo via OSAL */
    }
}

void osal_test_create(void)
{
    osal_id_t tid;
    int32 r = OS_TaskCreate(&tid, "OSAL_TEST",
                            osal_test_entry,
                            NULL,            /* stack dinámico */
                            2048,            /* 2 KB stack */
                            100,             /* prioridad OSAL media */
                            0);
    if (r != OS_SUCCESS) {
        uart_printf("[!] OS_TaskCreate FAIL r=%ld\r\n", (long)r);
    } else {
        uart_printf("[osal] OS_TaskCreate OK\r\n");
    }
}

