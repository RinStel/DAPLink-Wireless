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
#ifndef RADIO_PROTOCOL_H
#define RADIO_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define RADIO_PROTOCOL_HEADER_SIZE  17U
#define RADIO_PROTOCOL_PAYLOAD_SIZE 64U
#define RADIO_PROTOCOL_FRAME_SIZE \
    (RADIO_PROTOCOL_HEADER_SIZE + RADIO_PROTOCOL_PAYLOAD_SIZE)

typedef enum {
    RADIO_FRAME_DATA = 1,
    RADIO_FRAME_ACK,
    RADIO_FRAME_LINE_CODING,
    RADIO_FRAME_SWD_REQUEST,
    RADIO_FRAME_SWD_RESPONSE,
    RADIO_FRAME_PROFILE_SWITCH,
    RADIO_FRAME_PROFILE_CONFIRM,
    RADIO_FRAME_SESSION_START,
    RADIO_FRAME_HOP_SWITCH,
    RADIO_FRAME_HOP_CONFIRM
} radio_frame_type_t;

typedef struct {
    radio_frame_type_t type;
    uint32_t session;
    uint32_t sequence;
    const uint8_t *payload;
    uint8_t payload_length;
} radio_frame_view_t;

typedef struct {
    uint32_t session;
    uint32_t sequence;
    uint32_t payload_digest;
    radio_frame_type_t type;
    uint8_t payload_length;
} radio_frame_key_t;

uint8_t radio_protocol_build(uint8_t *frame, radio_frame_type_t type,
                             uint32_t network_id, uint32_t session,
                             uint32_t sequence, const uint8_t *payload,
                             uint8_t payload_length);
bool radio_protocol_parse(const uint8_t *frame, uint8_t frame_length,
                          uint32_t network_id,
                          radio_frame_view_t *view);
void radio_protocol_key_get(const radio_frame_view_t *view,
                            radio_frame_key_t *key);
bool radio_protocol_key_equal(const radio_frame_key_t *left,
                              const radio_frame_key_t *right);

#endif
