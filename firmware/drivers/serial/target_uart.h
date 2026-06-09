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
