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

#include "device_config.h"

typedef enum {
    SWD_OWNER_NONE = 0,
    SWD_OWNER_WIRED_HOST,
    SWD_OWNER_WIRELESS_SLAVE
} swd_owner_t;

#define SWD_BRIDGE_LOCAL_BATCH_BUDGET_US    400U
#define SWD_BRIDGE_WIRELESS_BATCH_BUDGET_US 1600U
#define SWD_BRIDGE_PENDING_BLOCK_QUEUE_SIZE 2U
#define SWD_BRIDGE_HOST_WINDOW              8U

/* 本服务把有线 CMSIS-DAP 和无线 SWD 流量串行化到唯一的目标 SWD 引擎。
 * 主机侧维护按序的期望事务 FIFO 和完成响应 FIFO，支持最多 4 个在途
 * SWD 事务；响应严格按请求顺序返回，因此按序匹配即可。 */

typedef struct {
    swd_tunnel_transfer_t transfers[SWD_TUNNEL_MAX_BLOCK_TRANSFERS];
    uint8_t count;
} host_block_t;

static swd_owner_t s_owner;
static uint8_t s_host_expected[SWD_BRIDGE_HOST_WINDOW];
static host_block_t s_host_blocks[SWD_BRIDGE_HOST_WINDOW];
static uint8_t s_host_head;
static uint8_t s_host_count;
static swd_tunnel_response_t s_host_responses[SWD_BRIDGE_HOST_WINDOW];
static bool s_host_response_ready[SWD_BRIDGE_HOST_WINDOW];
static uint8_t s_host_resp_head;
static uint8_t s_host_resp_count;
static uint8_t s_reply[SWD_TUNNEL_MAX_PAYLOAD];
static uint8_t s_reply_length;
static uint32_t s_cancellations;
static uint32_t s_stale_responses;
static bool s_reply_ready;
static bool s_reply_block;
static bool s_block_active;
static uint8_t s_block_count;
static uint8_t s_slave_transaction;
static swd_tunnel_transfer_t s_block_transfers[SWD_TUNNEL_MAX_BLOCK_TRANSFERS];
static swd_tunnel_block_t s_pending_blocks[SWD_BRIDGE_PENDING_BLOCK_QUEUE_SIZE];
static uint8_t s_pending_block_read;
static uint8_t s_pending_block_write;
static uint8_t s_pending_block_count;

static bool host_expected_push(uint8_t transaction_id)
{
    uint8_t tail;

    if (s_host_count >= SWD_BRIDGE_HOST_WINDOW) {
        return false;
    }
    tail = (uint8_t)((s_host_head + s_host_count) %
                     SWD_BRIDGE_HOST_WINDOW);
    s_host_expected[tail] = transaction_id;
    s_host_blocks[tail].count = 0U;
    ++s_host_count;
    return true;
}

static void host_expected_pop(void)
{
    s_host_head = (uint8_t)((s_host_head + 1U) % SWD_BRIDGE_HOST_WINDOW);
    --s_host_count;
}

static void host_response_push(const swd_tunnel_response_t *response)
{
    uint8_t tail;

    if (s_host_resp_count >= SWD_BRIDGE_HOST_WINDOW) {
        ++s_stale_responses;
        return;
    }
    tail = (uint8_t)((s_host_resp_head + s_host_resp_count) %
                     SWD_BRIDGE_HOST_WINDOW);
    s_host_responses[tail] = *response;
    s_host_response_ready[tail] = true;
    ++s_host_resp_count;
}

static bool pending_block_start(void)
{
    swd_tunnel_block_t *block;

    if ((s_pending_block_count == 0U) || (s_owner != SWD_OWNER_NONE) ||
        s_reply_ready ||
        !swd_tunnel_submit_block(
            s_pending_blocks[s_pending_block_read].transaction_id,
            s_pending_blocks[s_pending_block_read].transfers,
            s_pending_blocks[s_pending_block_read].count)) {
        return false;
    }
    block = &s_pending_blocks[s_pending_block_read];
    memcpy(s_block_transfers, block->transfers,
           (size_t)block->count * sizeof(s_block_transfers[0]));
    s_block_count = block->count;
    s_block_active = true;
    s_slave_transaction = block->transaction_id;
    s_owner = SWD_OWNER_WIRELESS_SLAVE;
    s_pending_block_read = (uint8_t)((s_pending_block_read + 1U) %
                                     SWD_BRIDGE_PENDING_BLOCK_QUEUE_SIZE);
    --s_pending_block_count;
    return true;
}

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
    s_host_head = 0U;
    s_host_count = 0U;
    s_host_resp_head = 0U;
    s_host_resp_count = 0U;
    memset(s_host_response_ready, 0, sizeof(s_host_response_ready));
    s_reply_ready = false;
    s_reply_length = 0U;
    s_reply_block = false;
    s_block_active = false;
    s_block_count = 0U;
    s_pending_block_read = 0U;
    s_pending_block_write = 0U;
    s_pending_block_count = 0U;
}

void swd_bridge_service_process(void)
{
    uint8_t payload[SWD_TUNNEL_MAX_PAYLOAD];
    uint8_t length;

    /* 解码一个已完成的隧道响应，并路由给当前所有者。 */
    swd_tunnel_process_budget(
        s_owner == SWD_OWNER_WIRELESS_SLAVE
            ? SWD_BRIDGE_WIRELESS_BATCH_BUDGET_US
            : SWD_BRIDGE_LOCAL_BATCH_BUDGET_US);
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
    } else if (s_owner == SWD_OWNER_WIRED_HOST) {
        swd_tunnel_response_t response;

        if (swd_tunnel_decode_response(payload, length, &response) &&
            (s_host_count != 0U)) {
            host_expected_pop();
            host_response_push(&response);
        } else {
            ++s_stale_responses;
        }
    }
    s_owner = SWD_OWNER_NONE;
}

bool swd_bridge_service_begin(device_mode_t mode,
                              const uint8_t *payload, uint8_t length)
{
    /* 有线模式立即提交；无线主机模式登记事务并等待对端响应。 */
    if ((payload == NULL) || (length < 2U)) {
        return false;
    }
    if (mode == DEVICE_MODE_WIRED) {
        if ((s_owner != SWD_OWNER_NONE) ||
            !swd_tunnel_submit(payload, length)) {
            return false;
        }
        if (!host_expected_push(payload[1])) {
            return false;
        }
        s_owner = SWD_OWNER_WIRED_HOST;
        return true;
    }
    if (mode != DEVICE_MODE_WIRELESS_HOST) {
        return false;
    }
    return host_expected_push(payload[1]);
}

bool swd_bridge_service_begin_block(device_mode_t mode, uint8_t transaction_id,
                                    const swd_tunnel_transfer_t *transfers,
                                    uint8_t count)
{
    uint8_t tail;

    if ((transfers == NULL) || (count == 0U) ||
        (count > SWD_TUNNEL_MAX_BLOCK_TRANSFERS) ||
        (mode != DEVICE_MODE_WIRED && mode != DEVICE_MODE_WIRELESS_HOST)) {
        return false;
    }
    if (mode == DEVICE_MODE_WIRED) {
        if ((s_owner != SWD_OWNER_NONE) ||
            !swd_tunnel_submit_block(transaction_id, transfers, count)) {
            return false;
        }
        if (!host_expected_push(transaction_id)) {
            return false;
        }
        s_owner = SWD_OWNER_WIRED_HOST;
        return true;
    }
    if (!host_expected_push(transaction_id)) {
        return false;
    }
    tail = (uint8_t)((s_host_head + s_host_count - 1U) %
                     SWD_BRIDGE_HOST_WINDOW);
    memcpy(s_host_blocks[tail].transfers, transfers,
           (size_t)count * sizeof(s_host_blocks[tail].transfers[0]));
    s_host_blocks[tail].count = count;
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
    s_slave_transaction = payload[1];
    s_owner = SWD_OWNER_WIRELESS_SLAVE;
    return true;
}

bool swd_bridge_service_wireless_block_request(const uint8_t *payload,
                                               uint8_t length)
{
    swd_tunnel_block_t block;

    if (!swd_tunnel_decode_block(payload, length, &block) ||
        (block.count > SWD_TUNNEL_MAX_BLOCK_TRANSFERS)) {
        return false;
    }
    if ((s_owner != SWD_OWNER_NONE) || s_reply_ready) {
        if (s_pending_block_count >= SWD_BRIDGE_PENDING_BLOCK_QUEUE_SIZE) {
            return false;
        }
        s_pending_blocks[s_pending_block_write] = block;
        s_pending_block_write = (uint8_t)((s_pending_block_write + 1U) %
                                          SWD_BRIDGE_PENDING_BLOCK_QUEUE_SIZE);
        ++s_pending_block_count;
        return true;
    }
    if (!swd_tunnel_submit_block(block.transaction_id, block.transfers,
                                 block.count)) {
        return false;
    }
    memcpy(s_block_transfers, block.transfers,
           (size_t)block.count * sizeof(s_block_transfers[0]));
    s_block_count = block.count;
    s_block_active = true;
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
    if ((s_host_count == 0U) ||
        (response.transaction_id != s_host_expected[s_host_head])) {
        ++s_stale_responses;
        return false;
    }
    host_expected_pop();
    host_response_push(&response);
    return true;
}

bool swd_bridge_service_wireless_block_response(const uint8_t *payload,
                                                uint8_t length)
{
    swd_tunnel_block_response_t block;
    swd_tunnel_response_t response;
    const host_block_t *host_block;
    uint8_t index;
    uint8_t read_index = 0U;

    if (!swd_tunnel_decode_block_response(payload, length, &block)) {
        return false;
    }
    if ((s_host_count == 0U) ||
        (block.transaction_id != s_host_expected[s_host_head])) {
        ++s_stale_responses;
        return false;
    }
    host_block = &s_host_blocks[s_host_head];
    if ((block.completed > host_block->count) ||
        (block.read_count > host_block->count)) {
        return false;
    }
    memset(&response, 0, sizeof(response));
    response.operation = SWD_TUNNEL_OP_BLOCK;
    response.transaction_id = block.transaction_id;
    response.completed = block.completed;
    response.ack = block.ack;
    for (index = 0U; index < block.completed; ++index) {
        if ((host_block->transfers[index].request & 0x02U) != 0U) {
            if (read_index >= block.read_count) {
                return false;
            }
            response.data[index] = block.data[read_index++];
        }
    }
    if (read_index != block.read_count) {
        return false;
    }
    host_expected_pop();
    host_response_push(&response);
    return true;
}

bool swd_bridge_service_wireless_abort(uint8_t transaction_id)
{
    if ((s_owner != SWD_OWNER_WIRELESS_SLAVE) ||
        (transaction_id != s_slave_transaction)) {
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
    if (!s_reply_ready) {
        return false;
    }
    memcpy(payload, s_reply, s_reply_length);
    *length = s_reply_length;
    s_reply_ready = false;
    s_block_active = false;
    if (s_owner == SWD_OWNER_NONE) {
        (void)pending_block_start();
    }
    return true;
}

bool swd_bridge_service_reply_is_block(void)
{
    return s_reply_block;
}

bool swd_bridge_service_response_take(swd_tunnel_response_t *response)
{
    if ((s_host_resp_count == 0U) ||
        !s_host_response_ready[s_host_resp_head]) {
        return false;
    }
    *response = s_host_responses[s_host_resp_head];
    s_host_response_ready[s_host_resp_head] = false;
    s_host_resp_head = (uint8_t)((s_host_resp_head + 1U) %
                                 SWD_BRIDGE_HOST_WINDOW);
    --s_host_resp_count;
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
    if ((s_host_count == 0U) ||
        (transaction_id != s_host_expected[s_host_head])) {
        return false;
    }
    host_expected_pop();
    ++s_cancellations;
    if (s_owner == SWD_OWNER_WIRED_HOST) {
        swd_tunnel_cancel();
        s_owner = SWD_OWNER_NONE;
    }
    return true;
}

bool swd_bridge_service_request_active(void)
{
    return (s_host_count != 0U) || (s_owner == SWD_OWNER_WIRELESS_SLAVE);
}

/* 包含主机等待响应、从机执行请求和尚未取走的响应。 */
bool swd_bridge_service_busy(void)
{
    return (s_host_count != 0U) || (s_host_resp_count != 0U) ||
           s_reply_ready || (s_owner != SWD_OWNER_NONE);
}

uint32_t swd_bridge_service_cancellations(void)
{
    return s_cancellations;
}

uint32_t swd_bridge_service_stale_responses(void)
{
    return s_stale_responses;
}
