/*
 * DAPLink-Wireless — Wireless CMSIS-DAP v2 debug probe firmware
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
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
