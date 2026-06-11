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
#ifndef TARGET_SWD_H
#define TARGET_SWD_H

#include <stdbool.h>
#include <stdint.h>

#define SWD_SEQUENCE_MAX_RESPONSE 60U

typedef enum {
    TARGET_SWD_ACK_OK = 1,
    TARGET_SWD_ACK_WAIT = 2,
    TARGET_SWD_ACK_FAULT = 4,
    TARGET_SWD_ACK_PROTOCOL = 7,
    TARGET_SWD_ACK_PARITY = 8
} target_swd_ack_t;

typedef void (*target_swd_poll_hook_t)(void);

void target_swd_init(uint32_t clock_hz);
void target_swd_configure(uint8_t idle_cycles, uint16_t retry_count,
                          uint8_t turnaround, bool data_phase);
void target_swd_disconnect(void);
bool target_swd_connect(uint32_t *idcode);
target_swd_ack_t target_swd_transfer(uint8_t request, uint32_t *data);
void target_swd_poll_hook_set(target_swd_poll_hook_t hook);
void target_swd_abort_request(void);
void target_swd_abort_clear(void);
bool target_swd_sequence(uint16_t bit_count, const uint8_t *data);
bool target_swd_sequence_transfer(const uint8_t *request,
                                  uint8_t request_length,
                                  uint8_t *response,
                                  uint8_t *response_length);
void target_swd_pins_set(uint8_t value, uint8_t select);
uint8_t target_swd_pins_read(void);
void target_swd_reset_pulse(uint32_t duration_ms);

#endif
