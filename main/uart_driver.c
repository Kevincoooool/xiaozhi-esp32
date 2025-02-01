/*
 * @Author: Kevincoooool
 * @Date: 2022-07-08 10:58:25
 * @Description:
 * @version:
 * @Filename: Do not Edit
 * @LastEditTime: 2022-07-20 18:42:32
 * @FilePath: \hello_world_s3\main\controller\uart_driver.c
 */
#include "uart_driver.h"

void Uart_Init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = 0};

    uart_param_config(UART_NUM_4G, &uart_config);
    uart_set_pin(UART_NUM_4G, TX_PIN_4G, RX_PIN_4G, -1, -1);
    uart_driver_install(UART_NUM_4G, UART_BUFFER_LENGTH, UART_BUFFER_LENGTH, 0, NULL, 0);
}

void Uart_Buffer_Get(size_t *size)
{
    uart_get_buffered_data_len(UART_NUM_4G, size);
}

void SerialSend(uint8_t *data, uint32_t size, uint32_t overtime)
{
    uart_write_bytes(UART_NUM_4G, data, size);
}

uint32_t SerialRecv(uint8_t *data, uint32_t size, uint32_t overtime)
{
    uint32_t recvCount = 0;
    recvCount = uart_read_bytes(UART_NUM_4G, data, size, overtime);
    return recvCount;
}
