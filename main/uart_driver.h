
#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif
#include "driver/uart.h"
#include "driver/gpio.h"
#define TX_PIN_4G               GPIO_NUM_13
#define RX_PIN_4G               GPIO_NUM_14
#define UART_NUM_4G             UART_NUM_2
#define UART_BUFFER_LENGTH      2048

void Uart_Init(void);
void Uart_Buffer_Get(size_t *size);
void SerialSend(uint8_t *data, uint32_t size, uint32_t overtime);

uint32_t SerialRecv(uint8_t *data, uint32_t size, uint32_t overtime);

#ifdef __cplusplus
}
#endif
#endif
