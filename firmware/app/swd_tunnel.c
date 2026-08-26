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
#include "swd_tunnel.h"

#include <stddef.h>
#include <string.h>

#include "board.h"
#include "target_swd.h"

/* 隧道线格式独立于 CMSIS-DAP。请求先校验，只有操作完成或确认取消后才
 * 生成响应。 */
#define SWD_TUNNEL_RESPONSE_HEADER_SIZE 4U
#define SWD_TRANSFER_MATCH_VALUE        0x10U
#define SWD_TRANSFER_MATCH_MASK         0x20U
#define SWD_TRANSFER_MISMATCH           0x10U
#define SWD_TRANSFER_APNDP              0x01U
#define SWD_TRANSFER_RNW                0x02U
#define SWD_DP_RDBUFF_READ              0x0EU
#define SWD_TUNNEL_MAX_MATCH_RETRIES    128U
#define SWD_TUNNEL_DEFAULT_WAIT_RETRIES 100U
#define SWD_TUNNEL_MAX_WAIT_RETRIES     1024U
#define SWD_TUNNEL_EXECUTION_BUDGET_MS 2500U
#define SWD_TUNNEL_SEQUENCE_DATA_OFFSET 4U
#define SWD_TUNNEL_MAX_SEQUENCE_BITS \
    480U

static uint8_t s_response[SWD_TUNNEL_MAX_PAYLOAD];
static uint8_t s_response_length;
static uint8_t s_request[SWD_TUNNEL_MAX_PAYLOAD];
static uint8_t s_request_length;
static uint8_t s_pending_value;
static uint8_t s_pending_select;
static uint8_t s_pending_transaction;
static uint32_t s_pending_deadline_cycles;
static bool s_pending;
static bool s_request_ready;
static bool s_executing;
static bool s_cancelled;
static bool s_response_ready;
static uint32_t s_match_mask;
static uint16_t s_match_retry;
static uint16_t s_transfer_wait_retry_limit =
    SWD_TUNNEL_DEFAULT_WAIT_RETRIES;
static uint16_t s_transfer_wait_retries;
static bool s_transfer_async;
static bool s_transfer_poll_pending;
static bool s_transfer_post_read;
static bool s_transfer_check_write;
typedef enum {
    TRANSFER_PHASE_NORMAL = 0,
    TRANSFER_PHASE_POST_READ,
    TRANSFER_PHASE_WRITE_CHECK
} transfer_phase_t;
static transfer_phase_t s_transfer_phase;
static uint8_t s_transfer_count;
static uint8_t s_transfer_index;
static uint8_t s_transfer_completed;
static uint32_t s_transfer_started_at;
static uint32_t s_transfer_data;

static void encode_u32_le(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static uint32_t decode_u32_le(const uint8_t *input)
{
    return input[0] |
           ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) |
           ((uint32_t)input[3] << 24);
}

static bool sequence_byte_count(uint16_t bit_count, uint8_t *byte_count)
{
    uint16_t bytes;

    if ((byte_count == NULL) || (bit_count == 0U) ||
        (bit_count > SWD_TUNNEL_MAX_SEQUENCE_BITS)) {
        return false;
    }
    bytes = (uint16_t)((bit_count + 7U) / 8U);
    *byte_count = (uint8_t)bytes;
    return true;
}

static bool swd_sequence_request_valid(const uint8_t *request,
                                       uint8_t request_length)
{
    uint16_t input_offset = 1U;
    uint16_t output_length = 1U;
    uint8_t sequence;

    if ((request == NULL) || (request_length == 0U)) {
        return false;
    }
    for (sequence = 0U; sequence < request[0]; ++sequence) {
        uint8_t info;
        uint8_t bit_count;
        uint8_t byte_count;

        if (input_offset >= request_length) {
            return false;
        }
        info = request[input_offset++];
        bit_count = info & 0x3FU;
        if (bit_count == 0U) {
            bit_count = 64U;
        }
        byte_count = (uint8_t)((bit_count + 7U) / 8U);
        if ((info & 0x80U) != 0U) {
            output_length += byte_count;
            if (output_length > SWD_SEQUENCE_MAX_RESPONSE) {
                return false;
            }
        } else {
            input_offset += byte_count;
            if (input_offset > request_length) {
                return false;
            }
        }
    }
    return input_offset == request_length;
}

static bool request_valid(const uint8_t *request, uint8_t request_length)
{
    uint8_t operation;

    /* 每个请求格式为 [operation, transaction_id, 操作数据]；拒绝缺失或多余
     * 字节，执行器不对长度进行猜测。 */
    if ((request == NULL) || (request_length < 2U)) {
        return false;
    }
    /* 立即操作同步访问目标 SWD 引脚。Transfer 和 Sequence 仍检查执行预算，
     * 防止长请求独占主循环。 */
    operation = request[0];
    if ((operation == SWD_TUNNEL_OP_CONNECT) ||
        (operation == SWD_TUNNEL_OP_DISCONNECT) ||
        (operation == SWD_TUNNEL_OP_RESET)) {
        return request_length == 2U;
    }
    if (operation == SWD_TUNNEL_OP_CONFIGURE) {
        return (request_length == 9U) &&
               (request[7] >= 1U) && (request[7] <= 4U) &&
               (request[8] <= 1U);
    }
    if (operation == SWD_TUNNEL_OP_PINS) {
        return request_length == 8U;
    }
    if (operation == SWD_TUNNEL_OP_SEQUENCE) {
        uint16_t bit_count;
        uint8_t byte_count;

        if (request_length < 5U) {
            return false;
        }
        bit_count = (uint16_t)request[2] |
                    ((uint16_t)request[3] << 8);
        return sequence_byte_count(bit_count, &byte_count) &&
               (request_length == (uint8_t)(
                   SWD_TUNNEL_SEQUENCE_DATA_OFFSET + byte_count));
    }
    if (operation == SWD_TUNNEL_OP_CLOCK) {
        return (request_length == 6U) &&
               (decode_u32_le(&request[2]) != 0U);
    }
    if (operation == SWD_TUNNEL_OP_TRANSFER) {
        uint8_t count;
        uint8_t index;

        if (request_length < 3U) {
            return false;
        }
        count = request[2];
        if ((count == 0U) ||
            (count > SWD_TUNNEL_MAX_TRANSFERS) ||
            (request_length != (uint8_t)(3U + count * 5U))) {
            return false;
        }
        for (index = 0U; index < count; ++index) {
            uint8_t transfer = request[3U + index * 5U];

            if ((((transfer & SWD_TRANSFER_MATCH_MASK) != 0U) &&
                 ((transfer & 0x02U) != 0U)) ||
                (((transfer & SWD_TRANSFER_MATCH_VALUE) != 0U) &&
                 ((transfer & 0x02U) == 0U))) {
                return false;
            }
        }
        return true;
    }
    if (operation == SWD_TUNNEL_OP_SWD_SEQUENCE) {
        return (request_length >= 3U) &&
               swd_sequence_request_valid(
                   &request[2], (uint8_t)(request_length - 2U));
    }
    return false;
}

uint8_t swd_tunnel_encode_connect(uint8_t transaction_id, uint8_t *payload)
{
    if (payload == NULL) {
        return 0U;
    }
    payload[0] = SWD_TUNNEL_OP_CONNECT;
    payload[1] = transaction_id;
    return 2U;
}

uint8_t swd_tunnel_encode_reset(uint8_t transaction_id, uint8_t *payload)
{
    if (payload == NULL) {
        return 0U;
    }
    payload[0] = SWD_TUNNEL_OP_RESET;
    payload[1] = transaction_id;
    return 2U;
}

uint8_t swd_tunnel_encode_sequence(uint8_t transaction_id,
                                   uint16_t bit_count,
                                   const uint8_t *data,
                                   uint8_t *payload)
{
    uint8_t byte_count;

    if ((payload == NULL) || (data == NULL) ||
        !sequence_byte_count(bit_count, &byte_count)) {
        return 0U;
    }
    payload[0] = SWD_TUNNEL_OP_SEQUENCE;
    payload[1] = transaction_id;
    payload[2] = (uint8_t)bit_count;
    payload[3] = (uint8_t)(bit_count >> 8);
    memcpy(&payload[SWD_TUNNEL_SEQUENCE_DATA_OFFSET], data, byte_count);
    return (uint8_t)(SWD_TUNNEL_SEQUENCE_DATA_OFFSET + byte_count);
}

uint8_t swd_tunnel_encode_swd_sequence(uint8_t transaction_id,
                                       const uint8_t *request,
                                       uint8_t request_length,
                                       uint8_t *payload)
{
    if ((payload == NULL) || (request == NULL) ||
        (request_length == 0U) ||
        (request_length > SWD_TUNNEL_MAX_PAYLOAD - 2U)) {
        return 0U;
    }
    payload[0] = SWD_TUNNEL_OP_SWD_SEQUENCE;
    payload[1] = transaction_id;
    memcpy(&payload[2], request, request_length);
    return (uint8_t)(2U + request_length);
}

uint8_t swd_tunnel_encode_clock(uint8_t transaction_id,
                                uint32_t clock_hz, uint8_t *payload)
{
    if ((payload == NULL) || (clock_hz == 0U)) {
        return 0U;
    }
    payload[0] = SWD_TUNNEL_OP_CLOCK;
    payload[1] = transaction_id;
    encode_u32_le(&payload[2], clock_hz);
    return 6U;
}

uint8_t swd_tunnel_encode_disconnect(uint8_t transaction_id,
                                     uint8_t *payload)
{
    if (payload == NULL) {
        return 0U;
    }
    payload[0] = SWD_TUNNEL_OP_DISCONNECT;
    payload[1] = transaction_id;
    return 2U;
}

uint8_t swd_tunnel_encode_configure(uint8_t transaction_id,
                                    uint8_t idle_cycles,
                                    uint16_t retry_count,
                                    uint16_t match_retry,
                                    uint8_t turnaround,
                                    bool data_phase,
                                    uint8_t *payload)
{
    if ((payload == NULL) || (turnaround < 1U) ||
        (turnaround > 4U)) {
        return 0U;
    }
    payload[0] = SWD_TUNNEL_OP_CONFIGURE;
    payload[1] = transaction_id;
    payload[2] = idle_cycles;
    payload[3] = (uint8_t)retry_count;
    payload[4] = (uint8_t)(retry_count >> 8);
    payload[5] = (uint8_t)match_retry;
    payload[6] = (uint8_t)(match_retry >> 8);
    payload[7] = turnaround;
    payload[8] = data_phase ? 1U : 0U;
    return 9U;
}

uint8_t swd_tunnel_encode_pins(uint8_t transaction_id,
                               uint8_t value, uint8_t select,
                               uint32_t wait_us, uint8_t *payload)
{
    if (payload == NULL) {
        return 0U;
    }
    payload[0] = SWD_TUNNEL_OP_PINS;
    payload[1] = transaction_id;
    payload[2] = value;
    payload[3] = select;
    encode_u32_le(&payload[4], wait_us);
    return 8U;
}

uint8_t swd_tunnel_encode_transfers(
    uint8_t transaction_id, const swd_tunnel_transfer_t *transfers,
    uint8_t count, uint8_t *payload)
{
    uint8_t index;

    if ((payload == NULL) || (transfers == NULL) || (count == 0U) ||
        (count > SWD_TUNNEL_MAX_TRANSFERS)) {
        return 0U;
    }
    payload[0] = SWD_TUNNEL_OP_TRANSFER;
    payload[1] = transaction_id;
    payload[2] = count;
    for (index = 0U; index < count; ++index) {
        uint8_t offset = (uint8_t)(3U + index * 5U);

        payload[offset] = transfers[index].request & 0x3FU;
        encode_u32_le(&payload[offset + 1U], transfers[index].data);
    }
    return (uint8_t)(3U + count * 5U);
}

static bool block_request_supported(uint8_t request)
{
    return !((((request & SWD_TRANSFER_MATCH_MASK) != 0U) &&
              ((request & 0x02U) != 0U)) ||
             (((request & SWD_TRANSFER_MATCH_VALUE) != 0U) &&
              ((request & 0x02U) == 0U)));
}

uint8_t swd_tunnel_encode_block(
    uint8_t transaction_id, const swd_tunnel_transfer_t *transfers,
    uint8_t count, uint8_t *payload)
{
    uint8_t index;
    uint8_t write_count = 0U;
    uint16_t length;
    uint16_t write_offset;

    if ((payload == NULL) || (transfers == NULL) || (count == 0U) ||
        (count > SWD_TUNNEL_MAX_BLOCK_TRANSFERS)) {
        return 0U;
    }
    length = (uint16_t)(2U + count);
    for (index = 0U; index < count; ++index) {
        uint8_t request = transfers[index].request & 0x3FU;

        if (!block_request_supported(request)) {
            return 0U;
        }
        if ((request & 0x02U) == 0U) {
            ++write_count;
        }
    }
    length = (uint16_t)(length + (uint16_t)write_count * 4U);
    if (length > SWD_TUNNEL_MAX_BLOCK_PAYLOAD) {
        return 0U;
    }
    payload[0] = transaction_id;
    payload[1] = count;
    write_offset = (uint16_t)(2U + count);
    for (index = 0U; index < count; ++index) {
        uint8_t request = transfers[index].request & 0x3FU;

        payload[2U + index] = request;
        if ((request & 0x02U) == 0U) {
            encode_u32_le(&payload[write_offset], transfers[index].data);
            write_offset = (uint16_t)(write_offset + 4U);
        }
    }
    return (uint8_t)length;
}

bool swd_tunnel_decode_block(const uint8_t *payload, uint8_t length,
                             swd_tunnel_block_t *block)
{
    uint8_t index;
    uint8_t write_count = 0U;
    uint16_t write_offset;
    uint16_t expected_length;

    if ((payload == NULL) || (block == NULL) || (length < 3U) ||
        (payload[1] == 0U) ||
        (payload[1] > SWD_TUNNEL_MAX_BLOCK_TRANSFERS)) {
        return false;
    }
    block->transaction_id = payload[0];
    block->count = payload[1];
    for (index = 0U; index < block->count; ++index) {
        uint8_t request = payload[2U + index];

        if (!block_request_supported(request)) {
            return false;
        }
        block->transfers[index].request = request;
        block->transfers[index].data = 0U;
        if ((request & 0x02U) == 0U) {
            ++write_count;
        }
    }
    expected_length = (uint16_t)(2U + block->count +
                                 (uint16_t)write_count * 4U);
    if ((expected_length != length) ||
        (expected_length > SWD_TUNNEL_MAX_BLOCK_PAYLOAD)) {
        return false;
    }
    write_offset = (uint16_t)(2U + block->count);
    for (index = 0U; index < block->count; ++index) {
        if ((block->transfers[index].request & 0x02U) == 0U) {
            block->transfers[index].data = decode_u32_le(&payload[write_offset]);
            write_offset = (uint16_t)(write_offset + 4U);
        }
    }
    return true;
}

uint8_t swd_tunnel_encode_block_response(
    uint8_t transaction_id, uint8_t completed, uint8_t ack,
    const uint32_t *data, uint8_t read_count, uint8_t *payload)
{
    uint8_t index;
    uint16_t length;

    if ((payload == NULL) || (completed > SWD_TUNNEL_MAX_BLOCK_TRANSFERS) ||
        (read_count > SWD_TUNNEL_MAX_BLOCK_TRANSFERS) ||
        ((read_count != 0U) && (data == NULL))) {
        return 0U;
    }
    length = (uint16_t)(4U + (uint16_t)read_count * 4U);
    if (length > SWD_TUNNEL_MAX_BLOCK_PAYLOAD) {
        return 0U;
    }
    payload[0] = transaction_id;
    payload[1] = completed;
    payload[2] = ack;
    payload[3] = read_count;
    for (index = 0U; index < read_count; ++index) {
        encode_u32_le(&payload[4U + index * 4U], data[index]);
    }
    return (uint8_t)length;
}

bool swd_tunnel_decode_block_response(
    const uint8_t *payload, uint8_t length,
    swd_tunnel_block_response_t *response)
{
    uint8_t index;

    if ((payload == NULL) || (response == NULL) || (length < 4U) ||
        (payload[1] > SWD_TUNNEL_MAX_BLOCK_TRANSFERS) ||
        (payload[3] > SWD_TUNNEL_MAX_BLOCK_TRANSFERS) ||
        (payload[3] > payload[1]) ||
        (length != (uint8_t)(4U + payload[3] * 4U))) {
        return false;
    }
    response->transaction_id = payload[0];
    response->completed = payload[1];
    response->ack = payload[2];
    response->read_count = payload[3];
    for (index = 0U; index < response->read_count; ++index) {
        response->data[index] = decode_u32_le(&payload[4U + index * 4U]);
    }
    return true;
}

static bool execute_immediate(const uint8_t *request,
                              uint8_t request_length,
                              uint8_t *response,
                              uint8_t *response_length)
{
    uint8_t operation;
    uint8_t transaction_id;
    uint8_t count = 0U;
    uint8_t completed = 0U;
    uint8_t raw_length = 0U;
    target_swd_ack_t ack = TARGET_SWD_ACK_OK;
    uint32_t started_at = board_millis();

    if ((request == NULL) || (request_length < 2U) ||
        (response == NULL) || (response_length == NULL)) {
        return false;
    }

    operation = request[0];
    transaction_id = request[1];
    if (operation == SWD_TUNNEL_OP_CONNECT) {
        if (request_length != 2U) {
            return false;
        }
        target_swd_init(100000U);
    } else if (operation == SWD_TUNNEL_OP_DISCONNECT) {
        if (request_length != 2U) {
            return false;
        }
        target_swd_disconnect();
    } else if (operation == SWD_TUNNEL_OP_CONFIGURE) {
        if ((request_length != 9U) || (request[7] < 1U) ||
            (request[7] > 4U) || (request[8] > 1U)) {
            return false;
        }
        s_match_retry =
            (uint16_t)request[5] | ((uint16_t)request[6] << 8);
        if (s_match_retry > SWD_TUNNEL_MAX_MATCH_RETRIES) {
            s_match_retry = SWD_TUNNEL_MAX_MATCH_RETRIES;
        }
        s_transfer_wait_retry_limit =
            (uint16_t)request[3] | ((uint16_t)request[4] << 8);
        if (s_transfer_wait_retry_limit > SWD_TUNNEL_MAX_WAIT_RETRIES) {
            s_transfer_wait_retry_limit = SWD_TUNNEL_MAX_WAIT_RETRIES;
        }
        target_swd_configure(
            request[2],
            (uint16_t)request[3] | ((uint16_t)request[4] << 8),
            request[7], request[8] != 0U);
    } else if (operation == SWD_TUNNEL_OP_PINS) {
        if (request_length != 8U) {
            return false;
        }
        target_swd_pins_set(request[2], request[3]);
        response[SWD_TUNNEL_RESPONSE_HEADER_SIZE] =
            target_swd_pins_read();
        response[SWD_TUNNEL_RESPONSE_HEADER_SIZE + 1U] = 0U;
        response[SWD_TUNNEL_RESPONSE_HEADER_SIZE + 2U] = 0U;
        response[SWD_TUNNEL_RESPONSE_HEADER_SIZE + 3U] = 0U;
        completed = 1U;
    } else if (operation == SWD_TUNNEL_OP_RESET) {
        if (request_length != 2U) {
            return false;
        }
        target_swd_reset_pulse(20U);
    } else if (operation == SWD_TUNNEL_OP_SEQUENCE) {
        uint16_t bit_count;
        uint8_t byte_count;

        if (request_length < 5U) {
            return false;
        }
        bit_count = (uint16_t)request[2] |
                    ((uint16_t)request[3] << 8);
        if (!sequence_byte_count(bit_count, &byte_count) ||
            (request_length != (uint8_t)(
                SWD_TUNNEL_SEQUENCE_DATA_OFFSET + byte_count)) ||
            !target_swd_sequence(
                bit_count, &request[SWD_TUNNEL_SEQUENCE_DATA_OFFSET])) {
            ack = TARGET_SWD_ACK_PROTOCOL;
        }
    } else if (operation == SWD_TUNNEL_OP_CLOCK) {
        if (request_length != 6U) {
            return false;
        }
        target_swd_init(decode_u32_le(&request[2]));
    } else if (operation == SWD_TUNNEL_OP_TRANSFER) {
        uint8_t index;
        bool check_write = false;

        if (request_length < 3U) {
            return false;
        }
        count = request[2];
        if ((count == 0U) || (count > SWD_TUNNEL_MAX_TRANSFERS) ||
            (request_length != (uint8_t)(3U + count * 5U))) {
            return false;
        }
        target_swd_abort_clear();
        for (index = 0U; index < count; ++index) {
            uint8_t offset = (uint8_t)(3U + index * 5U);
            uint8_t transfer_request = request[offset];
            uint32_t data = decode_u32_le(&request[offset + 1U]);

            if ((uint32_t)(board_millis() - started_at) >=
                SWD_TUNNEL_EXECUTION_BUDGET_MS) {
                ack = TARGET_SWD_ACK_WAIT;
                break;
            }
            if ((transfer_request & SWD_TRANSFER_MATCH_MASK) != 0U) {
                s_match_mask = data;
                ack = TARGET_SWD_ACK_OK;
            } else if ((transfer_request &
                        SWD_TRANSFER_MATCH_VALUE) != 0U) {
                uint32_t expected = data;
                uint16_t attempts = 0U;

                do {
                    ack = target_swd_transfer(transfer_request, &data);
                    if ((ack == TARGET_SWD_ACK_OK) &&
                        ((transfer_request & 0x03U) == 0x03U)) {
                        ack = target_swd_transfer(0x0EU, &data);
                    }
                    if (ack != TARGET_SWD_ACK_OK) {
                        break;
                    }
                    ++attempts;
                } while (((data & s_match_mask) != expected) &&
                         (attempts <= s_match_retry) &&
                         ((uint32_t)(board_millis() - started_at) <
                          SWD_TUNNEL_EXECUTION_BUDGET_MS));
                if ((ack == TARGET_SWD_ACK_OK) &&
                    ((data & s_match_mask) != expected)) {
                    ack = (target_swd_ack_t)(
                        (uint8_t)ack | SWD_TRANSFER_MISMATCH);
                }
                check_write = false;
            } else {
                ack = target_swd_transfer(transfer_request, &data);
                if ((ack == TARGET_SWD_ACK_OK) &&
                    ((transfer_request & 0x03U) == 0x03U)) {
                    ack = target_swd_transfer(0x0EU, &data);
                }
                check_write =
                    (transfer_request & 0x02U) == 0U;
            }
            if (ack != TARGET_SWD_ACK_OK) {
                break;
            }
            if ((transfer_request & SWD_TRANSFER_MATCH_MASK) == 0U) {
                encode_u32_le(
                    &response[SWD_TUNNEL_RESPONSE_HEADER_SIZE +
                              completed * 4U],
                    data);
            } else {
                encode_u32_le(
                    &response[SWD_TUNNEL_RESPONSE_HEADER_SIZE +
                              completed * 4U],
                    0U);
            }
            ++completed;
        }
        if ((completed == count) && check_write) {
            uint32_t ignored;

            ack = target_swd_transfer(0x0EU, &ignored);
        }
    } else if (operation == SWD_TUNNEL_OP_SWD_SEQUENCE) {
        if ((request_length < 3U) ||
            !swd_sequence_request_valid(&request[2],
                                        (uint8_t)(request_length - 2U)) ||
            !target_swd_sequence_transfer(
                &request[2], (uint8_t)(request_length - 2U),
                &response[SWD_TUNNEL_RESPONSE_HEADER_SIZE],
                &raw_length)) {
            return false;
        }
    } else {
        return false;
    }

    response[0] = operation;
    response[1] = transaction_id;
    response[2] = operation == SWD_TUNNEL_OP_SWD_SEQUENCE
                      ? raw_length
                      : completed;
    response[3] = (uint8_t)ack;
    *response_length = (uint8_t)(
        SWD_TUNNEL_RESPONSE_HEADER_SIZE +
        (operation == SWD_TUNNEL_OP_SWD_SEQUENCE
             ? raw_length
             : completed * 4U));
    return true;
}

static void transfer_async_finish(target_swd_ack_t ack)
{
    s_response[0] = SWD_TUNNEL_OP_TRANSFER;
    s_response[1] = s_request[1];
    s_response[2] = s_transfer_completed;
    s_response[3] = (uint8_t)ack;
    s_response_length = (uint8_t)(SWD_TUNNEL_RESPONSE_HEADER_SIZE +
                                  s_transfer_completed * 4U);
    s_transfer_async = false;
    s_transfer_poll_pending = false;
    s_executing = false;
    if (!s_cancelled) {
        s_response_ready = true;
    }
}

static void transfer_async_start(void)
{
    s_transfer_count = s_request[2];
    s_transfer_index = 0U;
    s_transfer_completed = 0U;
    s_transfer_started_at = board_millis();
    s_transfer_data = 0U;
    s_transfer_wait_retries = 0U;
    s_transfer_poll_pending = false;
    s_transfer_post_read = false;
    s_transfer_check_write = false;
    s_transfer_phase = TRANSFER_PHASE_NORMAL;
    s_transfer_async = true;
    s_executing = true;
    target_swd_abort_clear();
}

static bool transfer_is_plain_ap_read(uint8_t request)
{
    return (request & (SWD_TRANSFER_APNDP | SWD_TRANSFER_RNW |
                       SWD_TRANSFER_MATCH_VALUE)) ==
           (SWD_TRANSFER_APNDP | SWD_TRANSFER_RNW);
}

static void transfer_async_process(void)
{
    uint8_t offset = 0U;
    uint8_t request = 0U;
    target_swd_ack_t ack;
    target_swd_poll_result_t poll_result;

    if (s_cancelled || (uint32_t)(board_millis() - s_transfer_started_at) >=
                           SWD_TUNNEL_EXECUTION_BUDGET_MS) {
        target_swd_transfer_cancel();
        transfer_async_finish(TARGET_SWD_ACK_WAIT);
        return;
    }
    if (s_transfer_index < s_transfer_count) {
        offset = (uint8_t)(3U + s_transfer_index * 5U);
        request = s_request[offset];
    }
    if (!s_transfer_poll_pending) {
        uint8_t physical_request;

        if (s_transfer_post_read &&
            ((s_transfer_index >= s_transfer_count) ||
             !transfer_is_plain_ap_read(request))) {
            s_transfer_phase = TRANSFER_PHASE_POST_READ;
        } else if (s_transfer_index >= s_transfer_count) {
            if (!s_transfer_check_write) {
                transfer_async_finish(TARGET_SWD_ACK_OK);
                return;
            }
            s_transfer_phase = TRANSFER_PHASE_WRITE_CHECK;
        } else {
            if ((request & SWD_TRANSFER_MATCH_MASK) != 0U) {
                s_match_mask = decode_u32_le(&s_request[offset + 1U]);
                encode_u32_le(
                    &s_response[SWD_TUNNEL_RESPONSE_HEADER_SIZE +
                                s_transfer_completed * 4U],
                    0U);
                ++s_transfer_index;
                ++s_transfer_completed;
                s_transfer_wait_retries = 0U;
                return;
            }
            s_transfer_phase = TRANSFER_PHASE_NORMAL;
        }
        physical_request = s_transfer_phase == TRANSFER_PHASE_NORMAL
                               ? request
                               : SWD_DP_RDBUFF_READ;
        s_transfer_data = s_transfer_phase == TRANSFER_PHASE_NORMAL
                              ? decode_u32_le(&s_request[offset + 1U])
                              : 0U;
        if (!target_swd_transfer_begin(physical_request,
                                       &s_transfer_data)) {
            transfer_async_finish(TARGET_SWD_ACK_WAIT);
            return;
        }
        s_transfer_poll_pending = true;
        return;
    }
    poll_result = target_swd_transfer_poll(&ack);
    if (poll_result == TARGET_SWD_POLL_BUSY) {
        return;
    }
    s_transfer_poll_pending = false;
    if ((poll_result == TARGET_SWD_POLL_DONE) &&
        (ack == TARGET_SWD_ACK_WAIT) &&
        (s_transfer_wait_retries < s_transfer_wait_retry_limit)) {
        /* WAIT 只结束当前 SWD bit-bang 尝试；保留当前 index 和数据，在下次
         * 主循环重新发起同一 transfer，避免一次轮询长时间独占 USB/无线。 */
        ++s_transfer_wait_retries;
        return;
    }
    if ((poll_result != TARGET_SWD_POLL_DONE) ||
        (ack != TARGET_SWD_ACK_OK)) {
        transfer_async_finish(ack);
        return;
    }
    if (s_transfer_phase == TRANSFER_PHASE_POST_READ) {
        encode_u32_le(&s_response[SWD_TUNNEL_RESPONSE_HEADER_SIZE +
                                 (s_transfer_index - 1U) * 4U],
                      s_transfer_data);
        s_transfer_post_read = false;
    } else if (s_transfer_phase == TRANSFER_PHASE_WRITE_CHECK) {
        s_transfer_check_write = false;
    } else {
        if ((request & SWD_TRANSFER_MATCH_VALUE) != 0U &&
            ((s_transfer_data & s_match_mask) !=
             decode_u32_le(&s_request[offset + 1U]))) {
            transfer_async_finish((target_swd_ack_t)(
                (uint8_t)ack | SWD_TRANSFER_MISMATCH));
            return;
        }
        if (transfer_is_plain_ap_read(request)) {
            if (s_transfer_post_read) {
                encode_u32_le(
                    &s_response[SWD_TUNNEL_RESPONSE_HEADER_SIZE +
                                (s_transfer_index - 1U) * 4U],
                    s_transfer_data);
            }
            s_transfer_post_read = true;
            s_transfer_check_write = false;
        } else {
            encode_u32_le(
                &s_response[SWD_TUNNEL_RESPONSE_HEADER_SIZE +
                            s_transfer_index * 4U],
                s_transfer_data);
            s_transfer_check_write =
                (request & SWD_TRANSFER_RNW) == 0U;
        }
        ++s_transfer_index;
        ++s_transfer_completed;
    }
    s_transfer_wait_retries = 0U;
    if ((s_transfer_index >= s_transfer_count) &&
        !s_transfer_post_read && !s_transfer_check_write) {
        transfer_async_finish(TARGET_SWD_ACK_OK);
    }
}

bool swd_tunnel_submit(const uint8_t *request, uint8_t request_length)
{
    /* 返回前复制请求；成功后调用方可以立即复用 USB 或无线缓冲区。 */
    if (!request_valid(request, request_length) ||
        s_pending || s_request_ready || s_executing ||
        s_response_ready) {
        return false;
    }
    memcpy(s_request, request, request_length);
    s_request_length = request_length;
    s_request_ready = true;
    s_cancelled = false;
    return true;
}

void swd_tunnel_process(void)
{
    uint32_t wait_us;
    uint8_t pins;

    /* 每次调用只启动或推进一个操作，限制主循环延迟，并为取消长传输留出机会。 */
    if (s_transfer_async) {
        transfer_async_process();
        return;
    }
    if (s_request_ready) {
        s_request_ready = false;
        s_executing = true;
        if (s_request[0] == SWD_TUNNEL_OP_TRANSFER) {
            transfer_async_start();
            return;
        }
        if (s_request[0] != SWD_TUNNEL_OP_PINS) {
            bool executed =
                execute_immediate(s_request, s_request_length,
                                  s_response, &s_response_length);

            s_executing = false;
            if (executed && !s_cancelled) {
                s_response_ready = true;
            }
            return;
        }

        target_swd_pins_set(s_request[2], s_request[3]);
        pins = target_swd_pins_read();
        wait_us = decode_u32_le(&s_request[4]);
        s_executing = false;
        if (s_cancelled) {
            return;
        }
        if ((wait_us == 0U) ||
            (((pins ^ s_request[2]) & s_request[3] & 0x83U) == 0U)) {
            s_response[0] = SWD_TUNNEL_OP_PINS;
            s_response[1] = s_request[1];
            s_response[2] = 1U;
            s_response[3] = (uint8_t)TARGET_SWD_ACK_OK;
            encode_u32_le(
                &s_response[SWD_TUNNEL_RESPONSE_HEADER_SIZE], pins);
            s_response_length = SWD_TUNNEL_RESPONSE_HEADER_SIZE + 4U;
            s_response_ready = true;
            return;
        }
        s_pending_value = s_request[2];
        s_pending_select = s_request[3];
        s_pending_transaction = s_request[1];
        s_pending_deadline_cycles =
            board_cycle_count() + board_cycles_from_us(wait_us);
        s_pending = true;
    }
    if (!s_pending) {
        return;
    }
    pins = target_swd_pins_read();
    if ((((pins ^ s_pending_value) & s_pending_select & 0x83U) != 0U) &&
        ((int32_t)(board_cycle_count() -
                   s_pending_deadline_cycles) < 0)) {
        return;
    }
    s_response[0] = SWD_TUNNEL_OP_PINS;
    s_response[1] = s_pending_transaction;
    s_response[2] = 1U;
    s_response[3] = (uint8_t)TARGET_SWD_ACK_OK;
    encode_u32_le(&s_response[SWD_TUNNEL_RESPONSE_HEADER_SIZE], pins);
    s_response_length = SWD_TUNNEL_RESPONSE_HEADER_SIZE + 4U;
    s_pending = false;
    s_response_ready = true;
}

void swd_tunnel_cancel(void)
{
    /* 取消标志由后续 process 调用转换为确定性的响应。 */
    s_request_ready = false;
    s_pending = false;
    s_transfer_async = false;
    s_transfer_poll_pending = false;
    s_response_ready = false;
    s_cancelled = true;
    if (s_executing) {
        target_swd_transfer_cancel();
        target_swd_abort_request();
    }
}

bool swd_tunnel_response_take(uint8_t *response,
                              uint8_t *response_length)
{
    if ((response == NULL) || (response_length == NULL) ||
        !s_response_ready) {
        return false;
    }
    memcpy(response, s_response, s_response_length);
    *response_length = s_response_length;
    s_response_ready = false;
    return true;
}

bool swd_tunnel_decode_response(const uint8_t *payload, uint8_t length,
                                swd_tunnel_response_t *response)
{
    uint8_t index;

    if ((payload == NULL) || (response == NULL) ||
        (length < SWD_TUNNEL_RESPONSE_HEADER_SIZE)) {
        return false;
    }
    response->operation = payload[0];
    response->transaction_id = payload[1];
    response->completed = payload[2];
    response->ack = payload[3];
    response->raw_length = 0U;
    if (response->operation == SWD_TUNNEL_OP_SWD_SEQUENCE) {
        if ((payload[2] > sizeof(response->raw)) ||
            (length != (uint8_t)(SWD_TUNNEL_RESPONSE_HEADER_SIZE +
                                 payload[2]))) {
            return false;
        }
        memcpy(response->raw,
               &payload[SWD_TUNNEL_RESPONSE_HEADER_SIZE],
               payload[2]);
        response->raw_length = payload[2];
        return true;
    }
    if ((payload[2] > SWD_TUNNEL_MAX_TRANSFERS) ||
        (length != (uint8_t)(SWD_TUNNEL_RESPONSE_HEADER_SIZE +
                             payload[2] * 4U))) {
        return false;
    }
    for (index = 0U; index < response->completed; ++index) {
        response->data[index] =
            decode_u32_le(&payload[SWD_TUNNEL_RESPONSE_HEADER_SIZE +
                                   index * 4U]);
    }
    return true;
}
