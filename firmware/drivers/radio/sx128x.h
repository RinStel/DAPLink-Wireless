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
#ifndef SX128X_H
#define SX128X_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "radio_hal.h"

#define SX128X_MAX_PAYLOAD_SIZE 127U

typedef enum {
    SX128X_RESULT_OK = 0,
    SX128X_RESULT_INVALID_ARGUMENT,
    SX128X_RESULT_HAL_ERROR,
    SX128X_RESULT_COMMAND_ERROR,
    SX128X_RESULT_VERIFY_ERROR
} sx128x_result_t;

typedef enum {
    SX128X_MODE_RESERVED = 0,
    SX128X_MODE_STDBY_RC = 2,
    SX128X_MODE_STDBY_XOSC = 3,
    SX128X_MODE_FS = 4,
    SX128X_MODE_RX = 5,
    SX128X_MODE_TX = 6
} sx128x_mode_t;

typedef struct {
    uint8_t raw;
    sx128x_mode_t mode;
    uint8_t command_status;
} sx128x_status_t;

typedef enum {
    SX128X_PROFILE_GFSK_2M = 0,
    SX128X_PROFILE_GFSK_1M,
    SX128X_PROFILE_GFSK_500K,
    SX128X_PROFILE_FLRC_1M3,
    SX128X_PROFILE_FLRC_650K,
    SX128X_PROFILE_COUNT
} sx128x_profile_t;

typedef struct {
    int16_t rssi_dbm_x2;
    uint8_t error_status;
    uint8_t tx_rx_status;
    uint8_t sync_address_status;
} sx128x_packet_status_t;

enum {
    SX128X_IRQ_TX_DONE = 1U << 0,
    SX128X_IRQ_RX_DONE = 1U << 1,
    SX128X_IRQ_SYNC_WORD_VALID = 1U << 2,
    SX128X_IRQ_SYNC_WORD_ERROR = 1U << 3,
    SX128X_IRQ_CRC_ERROR = 1U << 6,
    SX128X_IRQ_RX_TX_TIMEOUT = 1U << 14,
    SX128X_IRQ_ALL = 0xFFFFU
};

sx128x_result_t sx128x_init_flrc(void);
sx128x_result_t sx128x_init_gfsk(void);
sx128x_result_t sx128x_set_frequency(uint32_t frequency_hz);
sx128x_result_t sx128x_set_profile(sx128x_profile_t profile);
sx128x_result_t sx128x_set_network_sync(const uint8_t sync_word[5]);
sx128x_profile_t sx128x_get_profile(void);
const char *sx128x_profile_name(sx128x_profile_t profile);
sx128x_result_t sx128x_get_status(sx128x_status_t *status);
sx128x_result_t sx128x_get_packet_status(sx128x_packet_status_t *status);
sx128x_result_t sx128x_get_irq_status(uint16_t *irq_status);
sx128x_result_t sx128x_clear_irq_status(uint16_t irq_mask);

sx128x_result_t sx128x_write_buffer(uint8_t offset,
                                    const uint8_t *data,
                                    size_t length);
sx128x_result_t sx128x_read_buffer(uint8_t offset,
                                   uint8_t *data,
                                   size_t length);
sx128x_result_t sx128x_get_rx_buffer_status(uint8_t *payload_length,
                                            uint8_t *buffer_offset);

sx128x_result_t sx128x_start_tx(const uint8_t *data, size_t length);
sx128x_result_t sx128x_start_rx(uint16_t timeout_ms);
sx128x_result_t sx128x_standby(void);

#endif
