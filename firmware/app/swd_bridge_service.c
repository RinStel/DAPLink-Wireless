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
#include "swd_bridge_service.h"

#include <stddef.h>
#include <string.h>

typedef enum {
    SWD_OWNER_NONE = 0,
    SWD_OWNER_WIRED_HOST,
    SWD_OWNER_WIRELESS_SLAVE
} swd_owner_t;

/* 本服务把有线 CMSIS-DAP 和无线 SWD 流量串行化到唯一的目标 SWD 引擎。 */

static swd_owner_t s_owner;
static bool s_request_active;
static bool s_response_ready;
static bool s_reply_ready;
static uint8_t s_expected_transaction;
static swd_tunnel_response_t s_response;
static uint8_t s_reply[SWD_TUNNEL_MAX_PAYLOAD];
static uint8_t s_reply_length;
static uint32_t s_cancellations;
static uint32_t s_stale_responses;
static bool s_block_active;
static bool s_reply_block;
static uint8_t s_block_count;
static swd_tunnel_transfer_t s_block_transfers[SWD_TUNNEL_MAX_BLOCK_TRANSFERS];

void swd_bridge_service_init(void)
{
    s_cancellations = 0U;
    s_stale_responses = 0U;
    swd_bridge_service_reset();
}

void swd_bridge_service_reset(void)
{
    swd_tunnel_cancel();
    s_owner = SWD_OWNER_NONE;
    s_request_active = false;
    s_response_ready = false;
    s_reply_ready = false;
    s_reply_length = 0U;
    s_block_active = false;
    s_reply_block = false;
    s_block_count = 0U;
}

void swd_bridge_service_process(void)
{
    uint8_t payload[SWD_TUNNEL_MAX_PAYLOAD];
    uint8_t length;

    /* 解码一个已完成的隧道响应，并路由给当前所有者。 */
    swd_tunnel_process();
    if (!swd_tunnel_response_take(payload, &length)) {
        return;
    }
    if (s_owner == SWD_OWNER_WIRELESS_SLAVE) {
        if (s_block_active) {
            swd_tunnel_response_t block_result;
            uint32_t reads[SWD_TUNNEL_MAX_BLOCK_TRANSFERS];
            uint8_t read_count = 0U;
            uint8_t index;

            if (!swd_tunnel_decode_response(payload, length, &block_result)) {
                s_owner = SWD_OWNER_NONE;
                return;
            }
            for (index = 0U; index < block_result.completed; ++index) {
                if ((s_block_transfers[index].request & 0x02U) != 0U) {
                    reads[read_count++] = block_result.data[index];
                }
            }
            s_reply_length = swd_tunnel_encode_block_response(
                block_result.transaction_id, block_result.completed,
                block_result.ack,
                reads, read_count, s_reply);
            s_reply_block = true;
        } else {
        memcpy(s_reply, payload, length);
        s_reply_length = length;
        s_reply_block = false;
        }
        s_reply_ready = true;
    } else if ((s_owner == SWD_OWNER_WIRED_HOST) &&
               swd_tunnel_decode_response(payload, length, &s_response)) {
        s_response_ready = true;
        s_request_active = false;
    }
    s_owner = SWD_OWNER_NONE;
}

bool swd_bridge_service_begin(device_mode_t mode,
                              const uint8_t *payload, uint8_t length)
{
    /* 有线模式立即提交；无线主机模式先保留请求，等待对端通过无线返回响应。 */
    if ((payload == NULL) || (length < 2U) || s_request_active) {
        return false;
    }
    s_expected_transaction = payload[1];
    s_request_active = true;
    s_response_ready = false;
    if (mode == DEVICE_MODE_WIRED) {
        if ((s_owner != SWD_OWNER_NONE) ||
            !swd_tunnel_submit(payload, length)) {
            s_request_active = false;
            return false;
        }
        s_owner = SWD_OWNER_WIRED_HOST;
        return true;
    }
    if (mode != DEVICE_MODE_WIRELESS_HOST) {
        s_request_active = false;
        return false;
    }
    return true;
}

bool swd_bridge_service_begin_block(device_mode_t mode, uint8_t transaction_id,
                                    const swd_tunnel_transfer_t *transfers,
                                    uint8_t count)
{
    if ((transfers == NULL) || (count == 0U) ||
        (count > SWD_TUNNEL_MAX_BLOCK_TRANSFERS) || s_request_active ||
        (mode != DEVICE_MODE_WIRED && mode != DEVICE_MODE_WIRELESS_HOST)) {
        return false;
    }
    memcpy(s_block_transfers, transfers,
           (size_t)count * sizeof(s_block_transfers[0]));
    s_block_count = count;
    s_block_active = true;
    s_expected_transaction = transaction_id;
    s_request_active = true;
    s_response_ready = false;
    if (mode == DEVICE_MODE_WIRED) {
        if ((s_owner != SWD_OWNER_NONE) ||
            !swd_tunnel_submit_block(transaction_id, transfers, count)) {
            s_request_active = false;
            s_block_active = false;
            return false;
        }
        s_owner = SWD_OWNER_WIRED_HOST;
    }
    return true;
}

bool swd_bridge_service_wireless_command(const uint8_t *payload,
                                         uint8_t length)
{
    if ((s_owner != SWD_OWNER_NONE) || s_reply_ready ||
        (payload == NULL) || (length < 2U) ||
        (payload[0] == SWD_TUNNEL_OP_BLOCK) ||
        !swd_tunnel_submit(payload, length)) {
        return false;
    }
    s_expected_transaction = payload[1];
    s_owner = SWD_OWNER_WIRELESS_SLAVE;
    return true;
}

bool swd_bridge_service_wireless_block_request(const uint8_t *payload,
                                               uint8_t length)
{
    swd_tunnel_block_t block;

    if ((s_owner != SWD_OWNER_NONE) || s_reply_ready ||
        !swd_tunnel_decode_block(payload, length, &block) ||
        (block.count > SWD_TUNNEL_MAX_BLOCK_TRANSFERS)) {
        return false;
    }
    if (!swd_tunnel_submit_block(block.transaction_id, block.transfers,
                                 block.count)) {
        return false;
    }
    memcpy(s_block_transfers, block.transfers,
           (size_t)block.count * sizeof(s_block_transfers[0]));
    s_block_count = block.count;
    s_block_active = true;
    s_expected_transaction = block.transaction_id;
    s_owner = SWD_OWNER_WIRELESS_SLAVE;
    return true;
}

bool swd_bridge_service_wireless_response(const uint8_t *payload,
                                          uint8_t length)
{
    swd_tunnel_response_t response;

    /* 其他事务的响应属于迟到数据，不得完成当前 CMSIS-DAP 命令。 */
    if (!swd_tunnel_decode_response(payload, length, &response)) {
        return false;
    }
    if (s_request_active &&
        (response.transaction_id == s_expected_transaction)) {
        s_response = response;
        s_response_ready = true;
        s_request_active = false;
    } else {
        ++s_stale_responses;
    }
    return true;
}

bool swd_bridge_service_wireless_block_response(const uint8_t *payload,
                                                uint8_t length)
{
    swd_tunnel_block_response_t block;
    uint8_t index;
    uint8_t read_index = 0U;

    if (!swd_tunnel_decode_block_response(payload, length, &block)) {
        return false;
    }
    if (!s_request_active || !s_block_active ||
        (block.transaction_id != s_expected_transaction) ||
        (block.completed > s_block_count) ||
        (block.read_count > s_block_count)) {
        ++s_stale_responses;
        return true;
    }
    memset(&s_response, 0, sizeof(s_response));
    s_response.operation = SWD_TUNNEL_OP_BLOCK;
    s_response.transaction_id = block.transaction_id;
    s_response.completed = block.completed;
    s_response.ack = block.ack;
    for (index = 0U; index < block.completed; ++index) {
        if ((s_block_transfers[index].request & 0x02U) != 0U) {
            if (read_index >= block.read_count) {
                return false;
            }
            s_response.data[index] = block.data[read_index++];
        }
    }
    if (read_index != block.read_count) {
        return false;
    }
    s_response_ready = true;
    s_request_active = false;
    s_block_active = false;
    return true;
}

bool swd_bridge_service_wireless_abort(uint8_t transaction_id)
{
    if ((s_owner != SWD_OWNER_WIRELESS_SLAVE) ||
        (transaction_id != s_expected_transaction)) {
        return false;
    }
    swd_tunnel_cancel();
    s_owner = SWD_OWNER_NONE;
    s_reply_ready = false;
    s_reply_length = 0U;
    s_block_active = false;
    s_block_count = 0U;
    ++s_cancellations;
    return true;
}

bool swd_bridge_service_reply_take(uint8_t *payload, uint8_t *length)
{
    if ((payload == NULL) || (length == NULL) || !s_reply_ready) {
        return false;
    }
    memcpy(payload, s_reply, s_reply_length);
    *length = s_reply_length;
    s_reply_ready = false;
    s_block_active = false;
    return true;
}

bool swd_bridge_service_reply_is_block(void)
{
    return s_reply_block;
}

bool swd_bridge_service_response_take(swd_tunnel_response_t *response)
{
    if ((response == NULL) || !s_response_ready) {
        return false;
    }
    *response = s_response;
    s_response_ready = false;
    s_block_active = false;
    s_reply_block = false;
    s_block_count = 0U;
    return true;
}

void swd_bridge_service_repeat_request(void)
{
    if ((s_owner == SWD_OWNER_NONE) && !s_reply_ready &&
        (s_reply_length != 0U)) {
        s_reply_ready = true;
    }
}

bool swd_bridge_service_cancel(uint8_t transaction_id)
{
    if (!s_request_active ||
        (transaction_id != s_expected_transaction)) {
        return false;
    }
    s_request_active = false;
    s_response_ready = false;
    s_block_active = false;
    s_block_count = 0U;
    ++s_cancellations;
    if (s_owner == SWD_OWNER_WIRED_HOST) {
        swd_tunnel_cancel();
        s_owner = SWD_OWNER_NONE;
    }
    return true;
}

bool swd_bridge_service_request_active(void)
{
    return s_request_active;
}

uint32_t swd_bridge_service_cancellations(void)
{
    return s_cancellations;
}

uint32_t swd_bridge_service_stale_responses(void)
{
    return s_stale_responses;
}
