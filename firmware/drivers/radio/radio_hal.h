#ifndef RADIO_HAL_H
#define RADIO_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RADIO_RESULT_OK = 0,
    RADIO_RESULT_INVALID_ARGUMENT,
    RADIO_RESULT_BUSY_TIMEOUT,
    RADIO_RESULT_SPI_TIMEOUT
} radio_result_t;

typedef enum {
    RADIO_FRONTEND_STANDBY = 0,
    RADIO_FRONTEND_RECEIVE,
    RADIO_FRONTEND_TRANSMIT
} radio_frontend_mode_t;

radio_result_t radio_hal_init(void);
radio_result_t radio_hal_reset(uint32_t timeout_ms);
radio_result_t radio_hal_wait_ready(uint32_t timeout_ms);
radio_result_t radio_hal_transaction(const uint8_t *tx_data,
                                     uint8_t *rx_data,
                                     size_t length,
                                     uint32_t timeout_ms);

void radio_hal_frontend_set(radio_frontend_mode_t mode);
bool radio_hal_irq_active(void);

#endif
