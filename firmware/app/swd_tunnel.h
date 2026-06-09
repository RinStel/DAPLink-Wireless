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
#ifndef SWD_TUNNEL_H
#define SWD_TUNNEL_H

#include <stdbool.h>
#include <stdint.h>

#define SWD_TUNNEL_MAX_TRANSFERS 10U
#define SWD_TUNNEL_MAX_PAYLOAD   64U

typedef enum {
    SWD_TUNNEL_OP_CONNECT = 1,
    SWD_TUNNEL_OP_TRANSFER,
    SWD_TUNNEL_OP_RESET,
    SWD_TUNNEL_OP_SEQUENCE,
    SWD_TUNNEL_OP_CLOCK,
    SWD_TUNNEL_OP_DISCONNECT,
    SWD_TUNNEL_OP_CONFIGURE,
    SWD_TUNNEL_OP_PINS,
    SWD_TUNNEL_OP_SWD_SEQUENCE
} swd_tunnel_operation_t;

typedef struct {
    uint8_t request;
    uint32_t data;
} swd_tunnel_transfer_t;

typedef struct {
    uint8_t operation;
    uint8_t transaction_id;
    uint8_t completed;
    uint8_t ack;
    uint32_t data[SWD_TUNNEL_MAX_TRANSFERS];
    uint8_t raw_length;
    uint8_t raw[SWD_TUNNEL_MAX_PAYLOAD - 4U];
} swd_tunnel_response_t;

uint8_t swd_tunnel_encode_connect(uint8_t transaction_id, uint8_t *payload);
uint8_t swd_tunnel_encode_reset(uint8_t transaction_id, uint8_t *payload);
uint8_t swd_tunnel_encode_sequence(uint8_t transaction_id,
                                   uint16_t bit_count,
                                   const uint8_t *data,
                                   uint8_t *payload);
uint8_t swd_tunnel_encode_swd_sequence(uint8_t transaction_id,
                                       const uint8_t *request,
                                       uint8_t request_length,
                                       uint8_t *payload);
uint8_t swd_tunnel_encode_clock(uint8_t transaction_id,
                                uint32_t clock_hz, uint8_t *payload);
uint8_t swd_tunnel_encode_disconnect(uint8_t transaction_id,
                                     uint8_t *payload);
uint8_t swd_tunnel_encode_configure(uint8_t transaction_id,
                                    uint8_t idle_cycles,
                                    uint16_t retry_count,
                                    uint16_t match_retry,
                                    uint8_t turnaround,
                                    bool data_phase,
                                    uint8_t *payload);
uint8_t swd_tunnel_encode_pins(uint8_t transaction_id,
                               uint8_t value, uint8_t select,
                               uint32_t wait_us, uint8_t *payload);
uint8_t swd_tunnel_encode_transfers(
    uint8_t transaction_id, const swd_tunnel_transfer_t *transfers,
    uint8_t count, uint8_t *payload);
bool swd_tunnel_submit(const uint8_t *request, uint8_t request_length);
void swd_tunnel_process(void);
void swd_tunnel_cancel(void);
bool swd_tunnel_response_take(uint8_t *response,
                              uint8_t *response_length);
bool swd_tunnel_decode_response(const uint8_t *payload, uint8_t length,
                                swd_tunnel_response_t *response);

#endif
