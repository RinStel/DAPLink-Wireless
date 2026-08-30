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

/* 线格式为 magic[2]、version、type、network/session/sequence（BE32），
 * 第 16 字节为 payload_length，第 17 字节开始为 payload。v4 移除
 * SWD Burst 帧，SWD 请求不再发送独立请求 ACK。不保留旧无线固件兼容
 * 路径。 */
#define RADIO_PROTOCOL_HEADER_SIZE  17U
#define RADIO_PROTOCOL_PAYLOAD_SIZE 110U
#define RADIO_PROTOCOL_VERSION       4U
#define RADIO_PROTOCOL_ACK_PAYLOAD_SIZE 17U
#define RADIO_PROTOCOL_ACK_COMPACT_PAYLOAD_SIZE 8U
#define RADIO_PROTOCOL_FRAME_SIZE \
    (RADIO_PROTOCOL_HEADER_SIZE + RADIO_PROTOCOL_PAYLOAD_SIZE)

#define RADIO_PROTOCOL_ACK_FLAG_HOP_VALID 0x01U

typedef enum {
    RADIO_FRAME_DATA = 1,
    RADIO_FRAME_ACK,
    RADIO_FRAME_LINE_CODING,
    RADIO_FRAME_SWD_COMMAND,
    RADIO_FRAME_SWD_COMMAND_RESPONSE,
    RADIO_FRAME_PROFILE_SWITCH,
    RADIO_FRAME_PROFILE_CONFIRM,
    RADIO_FRAME_SESSION_START,
    RADIO_FRAME_SWD_ABORT,
    RADIO_FRAME_SWD_BLOCK,
    RADIO_FRAME_SWD_BLOCK_RESPONSE,
    /* 诊断用：从机回显开关（1 字节开关量）。旧固件按未知类型静默丢弃。 */
    RADIO_FRAME_LOOPBACK} radio_frame_type_t;

typedef struct {
    uint32_t ack_next;
    uint32_t bitmap;
    uint8_t flags;
    uint8_t next_channel;
    int16_t rssi_dbm_x2;
    uint8_t error_status;
    uint8_t tx_rx_status;
    uint8_t sync_address_status;
    uint8_t profile;
    uint8_t current_channel;
} radio_protocol_ack_t;

typedef struct {
    radio_frame_type_t type;
    uint32_t session;
    uint32_t sequence;
    /* 指向调用方的 frame；解析不会复制 payload。 */
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
bool radio_protocol_ack_encode(uint8_t *payload, uint8_t capacity,
                               const radio_protocol_ack_t *ack);
bool radio_protocol_ack_encode_compact(
    uint8_t *payload, uint8_t capacity, const radio_protocol_ack_t *ack);
bool radio_protocol_ack_decode(const uint8_t *payload, uint8_t length,
                               radio_protocol_ack_t *ack);

#endif
