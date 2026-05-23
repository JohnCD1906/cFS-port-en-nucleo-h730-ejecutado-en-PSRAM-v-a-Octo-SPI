/* ============================================================
 * uart_debug.c
 * ============================================================ */

#include "libs/uart_debug.h"
#include "stm32h7xx_hal.h"
#include <string.h>
#include <stdio.h>

/* UART8 handle — declarado aquí, extern en quien lo necesite  */
extern UART_HandleTypeDef huart8;

/* ----------------------------------------------------------
 * uart_init
 * Configura UART8 a 115200 8N1 con FIFO y pin swap.
 * Llámala una sola vez antes de cualquier uart_printf.
 * ---------------------------------------------------------- */
void uart_init(void)
{
    /* Clock UART8 */
    __HAL_RCC_UART8_CLK_ENABLE();

    /* Pines: PE0 (RX) y PE1 (TX) como AF8 = UART8         */
    /* Con pin swap, el periférico los intercambia internamente */
    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF8_UART8;
    HAL_GPIO_Init(GPIOE, &gpio);

    /* UART8 config */
    huart8.Instance            = UART8;
    huart8.Init.BaudRate       = 115200;
    huart8.Init.WordLength     = UART_WORDLENGTH_8B;
    huart8.Init.StopBits       = UART_STOPBITS_1;
    huart8.Init.Parity         = UART_PARITY_NONE;
    huart8.Init.Mode           = UART_MODE_TX_RX;
    huart8.Init.HwFlowCtl      = UART_HWCONTROL_NONE;
    huart8.Init.OverSampling   = UART_OVERSAMPLING_16;
    huart8.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart8.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart8.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT;
    huart8.AdvancedInit.Swap           = UART_ADVFEATURE_SWAP_ENABLE;

    HAL_UART_Init(&huart8);

    /* Habilitar FIFO (reduce stalls en ráfagas cortas)     */
    HAL_UARTEx_EnableFifoMode(&huart8);
}

/* ----------------------------------------------------------
 * uart_putchar — envía un byte, polling sobre TXE
 * ---------------------------------------------------------- */
void uart_putchar(char c)
{
    /* Convertir \n a \r\n para PuTTY                       */
    if (c == '\n') {
        uart_putchar('\r');
    }
    while (!(UART8->ISR & USART_ISR_TXE_TXFNF));
    UART8->TDR = (uint8_t)c;
}

/* ----------------------------------------------------------
 * uart_puts — envía string terminada en '\0'
 * ---------------------------------------------------------- */
void uart_puts(const char *s)
{
    while (*s) {
        uart_putchar(*s++);
    }
}

/* ----------------------------------------------------------
 * uart_printf — printf mínimo sobre UART8
 *
 * Formatos soportados:
 *   %s  — string
 *   %c  — char
 *   %d  — int con signo (decimal)
 *   %u  — unsigned int (decimal)
 *   %x  — unsigned int (hex minúsculas)
 *   %X  — unsigned int (hex mayúsculas)
 *   %lX / %lu / %ld — long (32-bit) equivalentes
 *   %08X — ancho con zero-padding
 * ---------------------------------------------------------- */
void uart_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    uart_puts(buf);

    va_end(args);
}
