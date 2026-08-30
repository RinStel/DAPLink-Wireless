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
#include "cmsis_dap.h"

#include <stddef.h>
#include <string.h>

#include "board.h"
#include "dap_diagnostics.h"
#include "firmware_version.h"
#include "serial_bridge.h"
#include "target_swd.h"

/* 命令核心以 4 槽 FIFO 流水推进：SWD Transfer/Block 命令在无线链路上
 * 最多 4 个在途；控制命令（Connect/Clock/Configure 等）作为屏障，仅在
 * 无在途事务时推进；Immediate 命令在提交时同步完成。响应严格按命令
 * 顺序交付 USB。USB 回调只提交或复制数据。 */
#define DAP_INFO                  0x00U
#define DAP_HOST_STATUS           0x01U
#define DAP_CONNECT               0x02U
#define DAP_DISCONNECT            0x03U
#define DAP_TRANSFER_CONFIGURE    0x04U
#define DAP_TRANSFER              0x05U
#define DAP_TRANSFER_BLOCK        0x06U
#define DAP_TRANSFER_ABORT        0x07U
#define DAP_WRITE_ABORT           0x08U
#define DAP_DELAY                 0x09U
#define DAP_RESET_TARGET          0x0AU
#define DAP_SWJ_PINS              0x10U
#define DAP_SWJ_CLOCK             0x11U
#define DAP_SWJ_SEQUENCE          0x12U
#define DAP_SWD_CONFIGURE         0x13U
#define DAP_SWD_SEQUENCE          0x1DU
#define DAP_VENDOR_STATUS         0x80U
#define DAP_VENDOR_TRACE          0x81U

#define DAP_INFO_VENDOR           0x01U
#define DAP_INFO_PRODUCT          0x02U
#define DAP_INFO_SERIAL           0x03U
#define DAP_INFO_FW_VERSION       0x04U
#define DAP_INFO_PRODUCT_FW_VERSION 0x09U
#define DAP_INFO_CAPABILITIES     0xF0U
#define DAP_INFO_PACKET_COUNT     0xFEU
#define DAP_INFO_PACKET_SIZE      0xFFU
#define CMSIS_DAP_PROTOCOL_VERSION "2.1.2"

#define DAP_PORT_DISABLED         0x00U
#define DAP_PORT_SWD              0x01U
#define DAP_OK                    0x00U
#define DAP_ERROR                 0xFFU
#define DAP_TRANSFER_ERROR        0x08U
#define DAP_TRANSFER_RNW          0x02U
#define DAP_TRANSFER_MATCH_VALUE  0x10U
#define DAP_TRANSFER_MATCH_MASK   0x20U
#define DAP_TRANSFER_TIMESTAMP    0x80U
#define DAP_TRANSFER_UNSUPPORTED  DAP_TRANSFER_TIMESTAMP
#define DAP_OPERATION_TIMEOUT_MS  4000U
#define DAP_MAX_TRANSFERS         16U
#define DAP_COMMAND_QUEUE_SIZE    CMSIS_DAP_PACKET_COUNT
#define DAP_VENDOR_STATUS_VERSION 5U
#ifndef CMSIS_DAP_ADVERTISE_ATOMIC_COMMANDS
#define CMSIS_DAP_ADVERTISE_ATOMIC_COMMANDS 0
#endif

typedef enum {
    SLOT_KIND_IMMEDIATE = 0,
    SLOT_KIND_TRANSFER,
    SLOT_KIND_CONTROL,
    SLOT_KIND_DELAY
} dap_slot_kind_t;

typedef struct {
    uint8_t request[CMSIS_DAP_PACKET_SIZE];
    uint8_t length;
    dap_slot_kind_t kind;
    uint8_t transaction;
    bool dispatched;
    bool cancel_waiting;
    bool transfer_block;
    bool write_abort;
    uint8_t transfer_count;
    /* 每槽独立保存 transfer 表：流水线下多个块同时在途，共享暂存区会被
     * 后续派发的解析覆盖，导致响应的读数据映射错乱。 */
    swd_tunnel_transfer_t transfers[DAP_MAX_TRANSFERS];
    uint8_t response[CMSIS_DAP_PACKET_SIZE];
    uint8_t response_length;
    bool response_ready;
    uint32_t deadline;
} dap_slot_t;

static dap_slot_t s_slots[DAP_COMMAND_QUEUE_SIZE];
static uint8_t s_slot_head;
static uint8_t s_slot_count;
static uint8_t s_inflight_count;
static uint8_t s_transaction_id;
static bool s_connected;
static uint8_t s_idle_cycles;
static uint16_t s_retry_count;
static uint16_t s_match_retry;
static uint8_t s_turnaround;
static bool s_data_phase;
static volatile bool s_abort_requested;
static uint8_t s_parent_request[CMSIS_DAP_PACKET_SIZE];
static uint8_t s_parent_response[CMSIS_DAP_PACKET_SIZE];
static uint8_t s_parent_count;
static uint8_t s_parent_request_offset;
static uint8_t s_parent_request_length;
static uint8_t s_parent_response_offset;
static bool s_parent_active;
static bool s_parent_response_ready;

static void dispatch_pipelines(void);
static uint32_t decode_u32_le(const uint8_t *input);
static void encode_u32_le(uint8_t *output, uint32_t value);

static uint8_t slot_next(uint8_t index)
{
    ++index;
    return index == DAP_COMMAND_QUEUE_SIZE ? 0U : index;
}

static bool slot_push(const uint8_t *request, uint8_t length)
{
    dap_slot_t *slot;
    uint8_t tail;

    if ((request == NULL) || (length == 0U) ||
        (length > CMSIS_DAP_PACKET_SIZE) ||
        (s_slot_count >= DAP_COMMAND_QUEUE_SIZE)) {
        return false;
    }
    tail = (uint8_t)((s_slot_head + s_slot_count) % DAP_COMMAND_QUEUE_SIZE);
    slot = &s_slots[tail];
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->request, request, length);
    slot->length = length;
    switch (request[0]) {
    case DAP_TRANSFER:
    case DAP_TRANSFER_BLOCK:
    case DAP_WRITE_ABORT:
        slot->kind = SLOT_KIND_TRANSFER;
        break;
    case DAP_CONNECT:
    case DAP_DISCONNECT:
    case DAP_TRANSFER_CONFIGURE:
    case DAP_RESET_TARGET:
    case DAP_SWJ_CLOCK:
    case DAP_SWJ_SEQUENCE:
    case DAP_SWD_CONFIGURE:
    case DAP_SWD_SEQUENCE:
    case DAP_SWJ_PINS:
        slot->kind = SLOT_KIND_CONTROL;
        break;
    case DAP_DELAY:
        slot->kind = SLOT_KIND_DELAY;
        break;
    default:
        slot->kind = SLOT_KIND_IMMEDIATE;
        break;
    }
    ++s_slot_count;
    return true;
}

/* 找到第一个已派发且未完成的槽位；没有则返回 NULL。 */
static dap_slot_t *outstanding_slot(void)
{
    uint8_t index = s_slot_head;
    uint8_t scanned;

    for (scanned = 0U; scanned < s_slot_count; ++scanned) {
        dap_slot_t *slot = &s_slots[index];

        if (slot->dispatched && !slot->response_ready) {
            return slot;
        }
        index = slot_next(index);
    }
    return NULL;
}

static void slot_complete(dap_slot_t *slot, uint8_t response_length)
{
    slot->response_length = response_length;
    slot->response_ready = true;
}

static void slot_transfer_error(dap_slot_t *slot)
{
    slot->response[0] = slot->request[0];
    slot->response[1] = 0U;
    if (slot->transfer_block) {
        slot->response[2] = 0U;
        slot->response[3] = DAP_TRANSFER_ERROR;
        slot_complete(slot, 4U);
    } else {
        slot->response[2] = DAP_TRANSFER_ERROR;
        slot_complete(slot, 3U);
    }
}

static bool command_length(const uint8_t *p, uint8_t avail, uint8_t *out)
{
    uint16_t n, i;
    if (avail == 0U) return false;
    switch (p[0]) {
    case DAP_INFO: case DAP_CONNECT: case DAP_DISCONNECT:
    case DAP_TRANSFER_CONFIGURE: case DAP_TRANSFER_ABORT: case DAP_WRITE_ABORT:
    case DAP_RESET_TARGET: case DAP_SWJ_CLOCK:
    case DAP_VENDOR_STATUS:
        n = (p[0] == DAP_INFO || p[0] == DAP_CONNECT) ? 2U :
            (p[0] == DAP_TRANSFER_CONFIGURE ? 6U : (p[0] == DAP_WRITE_ABORT ? 6U : (p[0] == DAP_SWJ_CLOCK ? 5U : 1U)));
        break;
    case DAP_HOST_STATUS: n = 3U; break;
    case DAP_DELAY: n = 3U; break;
    case DAP_SWD_CONFIGURE: n = 2U; break;
    case DAP_SWJ_PINS: n = 7U; break;
    case DAP_SWJ_SEQUENCE:
        if (avail < 2U) return false;
        n = (uint16_t)(2U + ((p[1] ? p[1] : 256U) + 7U) / 8U);
        break;
    case DAP_SWD_SEQUENCE:
        if (avail < 2U) return false;
        n = 2U;
        for (i = 0U; i < p[1]; ++i) {
            uint8_t info;
            if (n >= avail) return false;
            info = p[n++];
            n = (uint16_t)(n + ((info & 0x3fU) ?
                                ((info & 0x3fU) + 7U) / 8U : 8U));
        }
        break;
    case DAP_TRANSFER:
        if (avail < 3U) return false;
        n = 3U;
        for (i = 0U; i < p[2]; ++i) {
            if (n >= avail) return false;
            if ((p[n] & 0x02U) == 0U || (p[n] & 0x10U) != 0U) n = (uint16_t)(n + 5U);
            else ++n;
        }
        break;
    case DAP_TRANSFER_BLOCK:
        if (avail < 5U || p[3] != 0U) return false;
        n = 5U;
        if (p[2] != 0U && (p[4] & 0x02U) == 0U) {
            n = (uint16_t)(n + (uint16_t)p[2] * 4U);
        }
        break;
    default: n=1U; break;
    }
    if (n > avail || n > CMSIS_DAP_PACKET_SIZE) return false;
    *out = (uint8_t)n;
    return true;
}

static uint32_t decode_u32_le(const uint8_t *input)
{
    return input[0] |
           ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) |
           ((uint32_t)input[3] << 24);
}

static void encode_u32_le(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static void command_vendor_status(dap_slot_t *slot)
{
    serial_bridge_status_t status;
    uint8_t flags = 0U;
    uint8_t *s_response = slot->response;

    serial_bridge_status_get(&status);
    if (status.radio_ready) {
        flags |= 0x01U;
    }
    if (status.error) {
        flags |= 0x02U;
    }
    if (status.swd_request_active) {
        flags |= 0x04U;
    }
    if (status.remote_metrics_valid) {
        flags |= 0x08U;
    }
    s_response[0] = DAP_VENDOR_STATUS;
    s_response[1] = DAP_VENDOR_STATUS_VERSION;
    s_response[2] = status.device_mode;
    s_response[3] = flags;
    s_response[4] = status.retries;
    encode_u32_le(&s_response[5], status.radio_recoveries);
    encode_u32_le(&s_response[9], status.swd_cancellations);
    encode_u32_le(&s_response[13], status.stale_swd_responses);
    encode_u32_le(&s_response[17], status.uart_rx_overruns);
    s_response[21] = (uint8_t)status.remote_rssi_dbm_x2;
    s_response[22] =
        (uint8_t)((uint16_t)status.remote_rssi_dbm_x2 >> 8);
    s_response[23] = status.radio_profile;
    s_response[24] = status.profile_switches;
    s_response[25] = status.remote_error_status;
    s_response[26] = status.remote_tx_rx_status;
    s_response[27] = status.remote_sync_status;
    s_response[28] = status.radio_channel;
    s_response[29] = status.channel_switches;
    s_response[30] = board_reset_cause();
    encode_u32_le(&s_response[31], board_millis());
    encode_u32_le(&s_response[35], status.radio_timeouts);
    encode_u32_le(&s_response[39], status.invalid_radio_frames);
    encode_u32_le(&s_response[43], status.peer_session_changes);
    slot_complete(slot, 47U);
}

static void command_vendor_trace(dap_slot_t *slot)
{
#if CMSIS_DAP_DIAGNOSTICS_ENABLE
    const uint8_t *s_request = slot->request;
    uint8_t s_request_length = slot->length;
    uint8_t *s_response = slot->response;

    s_response[0] = DAP_VENDOR_TRACE;
    s_response[1] = 1U;
    if ((s_request_length >= 2U) && (s_request[1] == 0U)) {
        dap_diagnostics_reset();
        s_response[2] = DAP_OK;
        slot_complete(slot, 3U);
    } else if ((s_request_length >= 3U) && (s_request[1] == 1U)) {
        s_response[2] = s_request[2];
        if (dap_diagnostics_page(s_request[2], &s_response[4], 60U) == 60U) {
            s_response[3] = 60U;
            slot_complete(slot, 64U);
        } else {
            slot->response[1] = DAP_ERROR;
            slot_complete(slot, 2U);
        }
    } else {
        slot->response[1] = DAP_ERROR;
        slot_complete(slot, 2U);
    }
#else
    (void)slot;
    slot->response[0] = 0xFFU;
    slot_complete(slot, 1U);
#endif
}

static void info_string(dap_slot_t *slot, const char *text)
{
    uint8_t length = (uint8_t)strlen(text);

    if (length > CMSIS_DAP_PACKET_SIZE - 3U) {
        length = CMSIS_DAP_PACKET_SIZE - 3U;
    }
    slot->response[1] = (uint8_t)(length + 1U);
    memcpy(&slot->response[2], text, length);
    slot->response[2U + length] = '\0';
    slot_complete(slot, (uint8_t)(3U + length));
}

static void command_info(dap_slot_t *slot)
{
    const uint8_t *s_request = slot->request;
    uint8_t s_request_length = slot->length;
    uint8_t *s_response = slot->response;
    uint8_t info_id;

    if (s_request_length < 2U) {
        slot->response[0] = DAP_INFO;
        slot->response[1] = DAP_ERROR;
        slot_complete(slot, 2U);
        return;
    }
    s_response[0] = DAP_INFO;
    info_id = s_request[1];
    if (info_id == DAP_INFO_VENDOR) {
        info_string(slot, "RinStel");
    } else if (info_id == DAP_INFO_PRODUCT) {
        info_string(slot, "CMSIS-DAP");
    } else if (info_id == DAP_INFO_SERIAL) {
        char serial[9];
        static const char digits[] = "0123456789ABCDEF";
        uint32_t value = board_device_id_hash();
        uint8_t index;

        for (index = 0U; index < 8U; ++index) {
            serial[7U - index] = digits[value & 0xFU];
            value >>= 4;
        }
        serial[8] = '\0';
        info_string(slot, serial);
    } else if (info_id == DAP_INFO_FW_VERSION) {
        info_string(slot, CMSIS_DAP_PROTOCOL_VERSION);
    } else if (info_id == DAP_INFO_PRODUCT_FW_VERSION) {
        info_string(slot, FIRMWARE_VERSION_STRING);
    } else if (info_id == DAP_INFO_CAPABILITIES) {
        s_response[1] = 2U;
        s_response[2] = 0x01U;
#if CMSIS_DAP_ADVERTISE_ATOMIC_COMMANDS
        s_response[2] |= 0x10U;
#endif
        s_response[3] = 0x01U;
        slot_complete(slot, 4U);
    } else if (info_id == DAP_INFO_PACKET_COUNT) {
        s_response[1] = 1U;
        s_response[2] = CMSIS_DAP_PACKET_COUNT;
        slot_complete(slot, 3U);
    } else if (info_id == DAP_INFO_PACKET_SIZE) {
        s_response[1] = 2U;
        s_response[2] = CMSIS_DAP_PACKET_SIZE;
        s_response[3] = 0U;
        slot_complete(slot, 4U);
    } else {
        s_response[1] = 0U;
        slot_complete(slot, 2U);
    }
}

/* 解析可变长度的 DAP_Transfer / DAP_TransferBlock 到所属槽位的表。 */
static bool transfer_parse(const uint8_t *request, uint8_t request_length,
                           uint8_t *count_out,
                           swd_tunnel_transfer_t *transfers)
{
    uint8_t count;
    uint8_t input_offset = 3U;
    uint8_t index;
    uint8_t read_count = 0U;

    if (request_length < 3U) {
        return false;
    }
    count = request[2];
    if ((count == 0U) || (count > DAP_MAX_TRANSFERS)) {
        *count_out = 0U;
        return count == 0U;
    }
    for (index = 0U; index < count; ++index) {
        uint8_t transfer_request;

        if (input_offset >= request_length) {
            return false;
        }
        transfer_request = request[input_offset++];
        if ((transfer_request & DAP_TRANSFER_UNSUPPORTED) != 0U) {
            return false;
        }
        if ((((transfer_request & DAP_TRANSFER_MATCH_MASK) != 0U) &&
             ((transfer_request & DAP_TRANSFER_RNW) != 0U)) ||
            (((transfer_request & DAP_TRANSFER_MATCH_VALUE) != 0U) &&
             ((transfer_request & DAP_TRANSFER_RNW) == 0U))) {
            return false;
        }
        transfers[index].request = transfer_request & 0x3FU;
        transfers[index].data = 0U;
        if (((transfer_request & DAP_TRANSFER_RNW) == 0U) ||
            ((transfer_request & DAP_TRANSFER_MATCH_VALUE) != 0U)) {
            if ((uint8_t)(request_length - input_offset) < 4U) {
                return false;
            }
            transfers[index].data =
                decode_u32_le(&request[input_offset]);
            input_offset = (uint8_t)(input_offset + 4U);
        } else if (++read_count > 15U) {
            return false;
        }
    }
    *count_out = count;
    return true;
}

static bool transfer_block_parse(const uint8_t *request,
                                 uint8_t request_length,
                                 uint8_t *count_out,
                                 swd_tunnel_transfer_t *transfers)
{
    uint16_t count;
    uint8_t request_byte;
    uint8_t input_offset = 5U;
    uint8_t index;

    if (request_length < 5U) {
        return false;
    }
    count = (uint16_t)request[2] |
            ((uint16_t)request[3] << 8);
    request_byte = request[4];
    if ((count > DAP_MAX_TRANSFERS) ||
        ((request_byte & 0xF0U) != 0U) ||
        (((request_byte & DAP_TRANSFER_RNW) != 0U) && (count > 15U))) {
        return false;
    }
    if (count == 0U) {
        *count_out = 0U;
        return true;
    }
    for (index = 0U; index < count; ++index) {
        transfers[index].request = request_byte & 0x0FU;
        transfers[index].data = 0U;
        if ((request_byte & DAP_TRANSFER_RNW) == 0U) {
            if ((uint8_t)(request_length - input_offset) < 4U) {
                return false;
            }
            transfers[index].data =
                decode_u32_le(&request[input_offset]);
            input_offset = (uint8_t)(input_offset + 4U);
        }
    }
    *count_out = (uint8_t)count;
    return true;
}

static bool dispatch_transfer_slot(dap_slot_t *slot)
{
    const uint8_t *s_request = slot->request;
    uint8_t s_request_length = slot->length;
    uint8_t count = 0U;
    bool parsed;

    if (!s_connected) {
        slot_transfer_error(slot);
        return true;
    }
    if (s_request[0] == DAP_WRITE_ABORT) {
        if (s_request_length < 6U) {
            slot->response[1] = DAP_ERROR;
            slot_complete(slot, 2U);
            return true;
        }
        slot->transfers[0].request = 0U;
        slot->transfers[0].data = decode_u32_le(&s_request[2]);
        count = 1U;
        parsed = true;
        slot->write_abort = true;
    } else if (s_request[0] == DAP_TRANSFER_BLOCK) {
        parsed = transfer_block_parse(s_request, s_request_length, &count,
                                      slot->transfers);
        slot->transfer_block = true;
    } else {
        parsed = transfer_parse(s_request, s_request_length, &count,
                                slot->transfers);
    }
    if (!parsed) {
        slot_transfer_error(slot);
        return true;
    }
    slot->transfer_count = count;
    if (count == 0U) {
        if (slot->write_abort) {
            slot->response[0] = DAP_WRITE_ABORT;
            slot->response[1] = DAP_ERROR;
            slot_complete(slot, 2U);
        } else if (slot->transfer_block) {
            slot->response[0] = DAP_TRANSFER_BLOCK;
            slot->response[1] = 0U;
            slot->response[2] = 0U;
            slot->response[3] = 0U;
            slot_complete(slot, 4U);
        } else {
            slot->response[0] = DAP_TRANSFER;
            slot->response[1] = 0U;
            slot->response[2] = 0U;
            slot_complete(slot, 3U);
        }
        return true;
    }
    /* 预填响应头；读数据按完成结果追加。 */
    slot->response[0] = s_request[0];
    if (slot->write_abort) {
        slot->response[0] = DAP_WRITE_ABORT;
        slot->response[1] = 0U;
        slot->response_length = 2U;
    } else if (slot->transfer_block) {
        slot->response[1] = 0U;
        slot->response[2] = 0U;
        slot->response[3] = 0U;
        slot->response_length = 4U;
    } else {
        slot->response[1] = 0U;
        slot->response[2] = 0U;
        slot->response_length = 3U;
    }
    slot->transaction = ++s_transaction_id;
    if (!serial_bridge_swd_transfers(slot->transaction, slot->transfers,
                                     count)) {
        /* 桥接窗口暂时满：保持未派发状态，下一轮主循环重试。 */
        --s_transaction_id;
        return false;
    }
    slot->dispatched = true;
    ++s_inflight_count;
    slot->deadline = board_millis() + DAP_OPERATION_TIMEOUT_MS;
    return true;
}

static bool dispatch_control_slot(dap_slot_t *slot)
{
    const uint8_t *s_request = slot->request;
    uint8_t s_request_length = slot->length;
    uint8_t *s_response = slot->response;
    uint8_t command = s_request[0];
    bool submitted = false;
    bool validate_error = false;

    slot->transaction = ++s_transaction_id;
    switch (command) {
    case DAP_CONNECT: {
        uint8_t port = s_request_length >= 2U ? s_request[1] : 0U;

        s_response[0] = DAP_CONNECT;
        if ((port != 0U) && (port != DAP_PORT_SWD)) {
            validate_error = true;
        } else {
            submitted = serial_bridge_swd_connect(slot->transaction);
        }
        break;
    }
    case DAP_DISCONNECT:
        s_response[0] = DAP_DISCONNECT;
        s_connected = false;
        submitted = serial_bridge_swd_disconnect(slot->transaction);
        break;
    case DAP_TRANSFER_CONFIGURE:
        if (s_request_length < 6U) {
            validate_error = true;
            break;
        }
        s_idle_cycles = s_request[1];
        s_retry_count = (uint16_t)s_request[2] |
                        ((uint16_t)s_request[3] << 8);
        s_match_retry = (uint16_t)s_request[4] |
                        ((uint16_t)s_request[5] << 8);
        s_response[0] = DAP_TRANSFER_CONFIGURE;
        submitted = serial_bridge_swd_configure(
            slot->transaction, s_idle_cycles, s_retry_count,
            s_match_retry, s_turnaround, s_data_phase);
        break;
    case DAP_SWD_CONFIGURE: {
        uint8_t value;

        if (s_request_length < 2U) {
            validate_error = true;
            break;
        }
        value = s_request[1];
        s_turnaround = (uint8_t)((value & 0x03U) + 1U);
        s_data_phase = (value & 0x04U) != 0U;
        s_response[0] = DAP_SWD_CONFIGURE;
        submitted = serial_bridge_swd_configure(
            slot->transaction, s_idle_cycles, s_retry_count,
            s_match_retry, s_turnaround, s_data_phase);
        break;
    }
    case DAP_SWJ_CLOCK:
        s_response[0] = DAP_SWJ_CLOCK;
        if ((s_request_length < 5U) ||
            (decode_u32_le(&s_request[1]) == 0U)) {
            validate_error = true;
        } else {
            submitted = serial_bridge_swd_clock(
                slot->transaction, decode_u32_le(&s_request[1]));
        }
        break;
    case DAP_SWJ_SEQUENCE: {
        uint16_t bit_count;
        uint8_t byte_count;

        s_response[0] = DAP_SWJ_SEQUENCE;
        if (s_request_length < 3U) {
            validate_error = true;
            break;
        }
        bit_count = s_request[1] == 0U ? 256U : s_request[1];
        byte_count = (uint8_t)((bit_count + 7U) / 8U);
        if ((s_request_length < (uint8_t)(2U + byte_count))) {
            validate_error = true;
        } else {
            submitted = serial_bridge_swd_sequence(
                slot->transaction, bit_count, &s_request[2]);
        }
        break;
    }
    case DAP_SWD_SEQUENCE: {
        uint8_t input_offset = 2U;
        uint8_t sequence;

        s_response[0] = DAP_SWD_SEQUENCE;
        if (s_request_length < 2U) {
            validate_error = true;
            break;
        }
        for (sequence = 0U; sequence < s_request[1]; ++sequence) {
            uint8_t info;
            uint8_t bit_count;
            uint8_t byte_count;

            if (input_offset >= s_request_length) {
                validate_error = true;
                break;
            }
            info = s_request[input_offset++];
            bit_count = info & 0x3FU;
            if (bit_count == 0U) {
                bit_count = 64U;
            }
            byte_count = (uint8_t)((bit_count + 7U) / 8U);
            if ((info & 0x80U) != 0U) {
                if ((uint8_t)(1U + byte_count) >=
                    CMSIS_DAP_PACKET_SIZE) {
                    validate_error = true;
                    break;
                }
            } else {
                if ((uint8_t)(s_request_length - input_offset) <
                    byte_count) {
                    validate_error = true;
                    break;
                }
                input_offset = (uint8_t)(input_offset + byte_count);
            }
        }
        if (!validate_error) {
            submitted = serial_bridge_swd_sequence_io(
                slot->transaction, &s_request[1],
                (uint8_t)(input_offset - 1U));
        }
        break;
    }
    case DAP_RESET_TARGET:
        s_response[0] = DAP_RESET_TARGET;
        submitted = serial_bridge_swd_reset(slot->transaction);
        break;
    case DAP_SWJ_PINS: {
        uint32_t wait_us;

        s_response[0] = DAP_SWJ_PINS;
        if (s_request_length < 7U) {
            validate_error = true;
            break;
        }
        wait_us = decode_u32_le(&s_request[3]);
        if (wait_us > 3000000U) {
            wait_us = 3000000U;
        }
        submitted = serial_bridge_swd_pins(
            slot->transaction, s_request[1], s_request[2], wait_us);
        break;
    }
    default:
        validate_error = true;
        break;
    }
    if (validate_error) {
        slot->response[1] = DAP_ERROR;
        slot_complete(slot, 2U);
        return true;
    }
    if (!submitted) {
        --s_transaction_id;
        return false;
    }
    slot->dispatched = true;
    ++s_inflight_count;
    slot->deadline = board_millis() + DAP_OPERATION_TIMEOUT_MS;
    return true;
}

static void dispatch_immediate_slot(dap_slot_t *slot)
{
    const uint8_t *s_request = slot->request;
    uint8_t s_request_length = slot->length;
    uint8_t *s_response = slot->response;
    uint8_t command = s_request[0];

    switch (command) {
    case DAP_INFO:
        command_info(slot);
        break;
    case DAP_HOST_STATUS:
        if ((s_request_length < 3U) || (s_request[1] > 1U) ||
            (s_request[2] > 1U)) {
            slot->response[0] = command;
            slot->response[1] = DAP_ERROR;
            slot_complete(slot, 2U);
        } else {
            s_response[0] = command;
            s_response[1] = DAP_OK;
            slot_complete(slot, 2U);
        }
        break;
    case DAP_VENDOR_STATUS:
        command_vendor_status(slot);
        break;
    case DAP_VENDOR_TRACE:
        command_vendor_trace(slot);
        break;
    case DAP_TRANSFER_ABORT:
        cmsis_dap_abort();
        slot->response[0] = command;
        slot->response[1] = DAP_ERROR;
        slot_complete(slot, 2U);
        break;
    default:
        s_response[0] = 0xFFU;
        slot_complete(slot, 1U);
        break;
    }
}

static void dispatch_pipelines(void)
{
    uint8_t index = s_slot_head;
    uint8_t scanned;

    for (scanned = 0U; scanned < s_slot_count; ++scanned) {
        dap_slot_t *slot = &s_slots[index];

        if (slot->response_ready) {
            index = slot_next(index);
            continue;
        }
        if (slot->dispatched) {
            /* 桥接在途或屏障未完成：按序等待。 */
            if (slot->kind != SLOT_KIND_TRANSFER) {
                break;
            }
            index = slot_next(index);
            continue;
        }
        switch (slot->kind) {
        case SLOT_KIND_TRANSFER:
            if (!dispatch_transfer_slot(slot)) {
                return;
            }
            break;
        case SLOT_KIND_CONTROL:
            if (s_inflight_count != 0U) {
                return;
            }
            if (!dispatch_control_slot(slot)) {
                return;
            }
            break;
        case SLOT_KIND_DELAY:
            if (s_inflight_count != 0U) {
                return;
            }
            slot->dispatched = true;
            slot->deadline = board_millis() +
                             (uint32_t)((uint16_t)slot->request[1] |
                                        ((uint16_t)slot->request[2] << 8));
            break;
        default:
            dispatch_immediate_slot(slot);
            break;
        }
        index = slot_next(index);
    }
}

static void complete_control_slot(dap_slot_t *slot,
                                  const swd_tunnel_response_t *result)
{
    uint8_t *s_response = slot->response;
    uint8_t command = slot->request[0];
    uint8_t ok = result->ack == TARGET_SWD_ACK_OK ? DAP_OK : DAP_ERROR;

    switch (command) {
    case DAP_CONNECT:
        s_connected = result->ack == TARGET_SWD_ACK_OK;
        s_response[1] = s_connected ? DAP_PORT_SWD : DAP_PORT_DISABLED;
        slot_complete(slot, 2U);
        break;
    case DAP_DISCONNECT:
        s_response[1] = DAP_OK;
        slot_complete(slot, 2U);
        break;
    case DAP_RESET_TARGET:
        s_response[1] = ok;
        s_response[2] = 1U;
        slot_complete(slot, 3U);
        break;
    case DAP_SWJ_PINS:
        s_response[1] =
            result->completed != 0U ? (uint8_t)result->data[0] : 0U;
        slot_complete(slot, 2U);
        break;
    case DAP_SWD_SEQUENCE:
        if ((result->operation != SWD_TUNNEL_OP_SWD_SEQUENCE) ||
            (result->raw_length == 0U) ||
            (result->raw_length >= CMSIS_DAP_PACKET_SIZE)) {
            s_response[1] = DAP_ERROR;
            slot_complete(slot, 2U);
        } else {
            memcpy(&s_response[1], result->raw, result->raw_length);
            slot_complete(slot, (uint8_t)(1U + result->raw_length));
        }
        break;
    default:
        s_response[1] = ok;
        slot_complete(slot, 2U);
        break;
    }
}

static void complete_transfer_slot(dap_slot_t *slot,
                                   const swd_tunnel_response_t *result)
{
    uint8_t index;

    if (slot->write_abort) {
        slot->response[1] =
            result->ack == TARGET_SWD_ACK_OK ? DAP_OK : DAP_ERROR;
        slot_complete(slot, 2U);
        return;
    }
    if ((result->operation != SWD_TUNNEL_OP_BLOCK) ||
        (result->completed > slot->transfer_count)) {
        slot_transfer_error(slot);
        return;
    }
    for (index = 0U; index < result->completed; ++index) {
        if ((slot->transfers[index].request & DAP_TRANSFER_RNW) != 0U &&
            (slot->transfers[index].request & DAP_TRANSFER_MATCH_VALUE) ==
                0U) {
            if (slot->response_length >
                CMSIS_DAP_PACKET_SIZE - 4U) {
                slot_transfer_error(slot);
                return;
            }
            encode_u32_le(&slot->response[slot->response_length],
                          result->data[index]);
            slot->response_length =
                (uint8_t)(slot->response_length + 4U);
        }
    }
    if (slot->transfer_block) {
        slot->response[1] = result->completed;
        slot->response[2] = 0U;
        slot->response[3] = result->ack;
    } else {
        slot->response[1] = result->completed;
        slot->response[2] = result->ack;
    }
    slot_complete(slot, slot->response_length);
}

static void drain_responses(void)
{
    swd_tunnel_response_t result;

    while (serial_bridge_swd_response_take(&result)) {
        dap_slot_t *slot = outstanding_slot();
        bool is_transfer;


        if ((slot == NULL) ||
            (result.transaction_id != slot->transaction)) {
            /* 迟到的重复响应：桥接层已按事务 ID 匹配，这里兜底丢弃。 */
            continue;
        }
        is_transfer = slot->kind == SLOT_KIND_TRANSFER;
        if (is_transfer) {
            complete_transfer_slot(slot, &result);
        } else {
            complete_control_slot(slot, &result);
        }
        --s_inflight_count;
    }
}

static void parent_start_next(void)
{
    uint8_t len;

    if (s_parent_request_offset >= s_parent_request_length) {
        /* 全部子命令响应已按序拼装完成。 */
        s_parent_active = false;
        s_parent_response_ready = true;
        return;
    }
    if (!command_length(&s_parent_request[s_parent_request_offset],
                        (uint8_t)(s_parent_request_length -
                                  s_parent_request_offset), &len) ||
        !slot_push(&s_parent_request[s_parent_request_offset], len)) {
        s_parent_active = false;
        s_parent_response[0] = 0xFFU;
        s_parent_response_offset = 1U;
        s_parent_response_ready = true;
        return;
    }
    s_parent_request_offset = (uint8_t)(s_parent_request_offset + len);
}

/* 父命令的子响应按序拼装；子命令槽在拼装后立即释放（不等待 USB）。
 * 循环直到没有新进展：子命令同步完成（Immediate/零传输）时继续拼装，
 * 异步子命令在途或派发受阻时返回，等待后续推进。 */
static void collect_parent_response(void)
{
    while (s_parent_active) {
        while ((s_slot_count != 0U) &&
               s_slots[s_slot_head].response_ready) {
            dap_slot_t *slot = &s_slots[s_slot_head];
            uint8_t length = slot->response_length;

            if ((uint16_t)s_parent_response_offset + length >
                CMSIS_DAP_PACKET_SIZE) {
                s_parent_active = false;
                s_parent_response[0] = 0xFFU;
                s_parent_response_offset = 1U;
                s_parent_response_ready = true;
                /* 剩余子命令尚未派发（子命令按序完成），可直接丢弃。 */
                s_slot_head = 0U;
                s_slot_count = 0U;
                s_inflight_count = 0U;
                return;
            }
            memcpy(&s_parent_response[s_parent_response_offset],
                   slot->response, length);
            s_parent_response_offset =
                (uint8_t)(s_parent_response_offset + length);
            s_slot_head = slot_next(s_slot_head);
            --s_slot_count;
            parent_start_next();
        }
        if (!s_parent_active) {
            return;
        }
        dispatch_pipelines();
        if ((s_slot_count == 0U) ||
            !s_slots[s_slot_head].response_ready) {
            /* 队头在途或派发受阻：本轮无新进展。 */
            return;
        }
    }
}

void cmsis_dap_init(void)
{
    s_slot_head = 0U;
    s_slot_count = 0U;
    s_inflight_count = 0U;
    s_transaction_id = 0U;
    s_idle_cycles = 0U;
    s_retry_count = 100U;
    s_match_retry = 0U;
    s_turnaround = 1U;
    s_data_phase = false;
    s_abort_requested = false;
    s_connected = false;
    s_parent_active = false;
    s_parent_response_ready = false;
}

bool cmsis_dap_submit(const uint8_t *request, uint8_t length)
{
    if ((request == NULL) || (length == 0U) ||
        (length > CMSIS_DAP_PACKET_SIZE)) {
        return false;
    }
    if (request[0] == 0x7FU) {
        /* ExecuteCommands：仅在流水线完全空闲时启动。 */
        if (s_parent_active || (s_slot_count != 0U) ||
            (length < 2U) || request[1] == 0U || request[1] > 32U ||
            length > sizeof(s_parent_request)) {
            return false;
        }
        memcpy(s_parent_request, request, length);
        s_parent_count = request[1];
        s_parent_request_offset = 2U;
        s_parent_request_length = length;
        s_parent_response_offset = 2U;
        s_parent_response[0] = 0x7FU;
        s_parent_response[1] = s_parent_count;
        s_parent_active = true;
        parent_start_next();
        collect_parent_response();
        return true;
    }
    if (!slot_push(request, length)) {
        return false;
    }
    dispatch_pipelines();
    return true;
}

void cmsis_dap_abort(void)
{
    s_abort_requested = true;
    target_swd_abort_request();
}

void cmsis_dap_process(void)
{
    dap_slot_t *slot;

    if (s_abort_requested) {
        s_abort_requested = false;
        slot = outstanding_slot();
        if ((slot != NULL) && (slot->kind == SLOT_KIND_TRANSFER) &&
            !slot->cancel_waiting) {
            serial_bridge_swd_cancel(slot->transaction);
            slot->cancel_waiting = true;
            slot->deadline = board_millis() + DAP_OPERATION_TIMEOUT_MS;
        }
    }
    slot = outstanding_slot();
    if ((slot != NULL) && slot->cancel_waiting) {
        /* Abort 采用协作式完成：保持原响应格式，在桥接取消完成或超时后
         * 报告传输错误。 */
        if (serial_bridge_swd_cancel_complete(slot->transaction) ||
            ((int32_t)(board_millis() - slot->deadline) >= 0)) {
            slot->cancel_waiting = false;
            if (slot->write_abort) {
                slot->response[0] = DAP_WRITE_ABORT;
                slot->response[1] = DAP_ERROR;
                slot_complete(slot, 2U);
            } else {
                slot_transfer_error(slot);
            }
            --s_inflight_count;
        }
    }
    serial_bridge_swd_pump();
    drain_responses();
    collect_parent_response();
    slot = outstanding_slot();
    if (slot != NULL) {
        if (slot->kind == SLOT_KIND_DELAY) {
            if ((int32_t)(board_millis() - slot->deadline) >= 0) {
                slot->response[0] = DAP_DELAY;
                slot->response[1] = DAP_OK;
                slot_complete(slot, 2U);
            }
        } else if ((int32_t)(board_millis() - slot->deadline) >= 0) {
            /* 超时终止队头命令：先取消桥接操作，再生成错误响应。 */
            serial_bridge_swd_cancel(slot->transaction);
            if (slot->kind == SLOT_KIND_TRANSFER) {
                slot_transfer_error(slot);
            } else {
                /* 与 Arm 实现一致：Disconnect 超时仍返回 OK。 */
                slot->response[1] = slot->request[0] == DAP_DISCONNECT
                                        ? DAP_OK
                                        : DAP_ERROR;
                slot_complete(slot, 2U);
            }
            --s_inflight_count;
        }
    }
    dispatch_pipelines();
}

bool cmsis_dap_busy(void)
{
    return (s_slot_count != 0U) || s_parent_active || s_parent_response_ready;
}

uint8_t cmsis_dap_response_pending_count(void)
{
    uint8_t count = s_parent_response_ready ? 1U : 0U;
    uint8_t index = s_slot_head;
    uint8_t scanned;

    for (scanned = 0U; scanned < s_slot_count; ++scanned) {
        if (s_slots[index].response_ready) {
            ++count;
        }
        index = slot_next(index);
    }
    return count;
}

bool cmsis_dap_response_take(uint8_t *response, uint8_t *length)
{
    if ((response == NULL) || (length == NULL)) {
        return false;
    }
    collect_parent_response();
    if (s_parent_response_ready) {
        *length = s_parent_response_offset;
        memcpy(response, s_parent_response, s_parent_response_offset);
        s_parent_response_ready = false;
        return true;
    }
    if (s_parent_active || (s_slot_count == 0U) ||
        !s_slots[s_slot_head].response_ready) {
        return false;
    }
    *length = s_slots[s_slot_head].response_length;
    memcpy(response, s_slots[s_slot_head].response, *length);
    s_slots[s_slot_head].dispatched = false;
    s_slots[s_slot_head].response_ready = false;
    s_slot_head = slot_next(s_slot_head);
    --s_slot_count;
    dispatch_pipelines();
    return true;
}
