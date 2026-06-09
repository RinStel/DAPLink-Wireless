#ifndef TARGET_UART_H
#define TARGET_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TARGET_UART_PARITY_NONE = 0,
    TARGET_UART_PARITY_ODD,
    TARGET_UART_PARITY_EVEN
} target_uart_parity_t;

typedef struct {
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    target_uart_parity_t parity;
} target_uart_config_t;

bool target_uart_init(const target_uart_config_t *config);
bool target_uart_configure(const target_uart_config_t *config);
void target_uart_process(void);
size_t target_uart_read(uint8_t *data, size_t capacity);
size_t target_uart_write(const uint8_t *data, size_t length);
size_t target_uart_tx_free(void);
const target_uart_config_t *target_uart_config(void);
uint32_t target_uart_rx_overruns(void);

#endif
