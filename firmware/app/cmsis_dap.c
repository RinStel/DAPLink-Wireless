#include "cmsis_dap.h"

#include <stddef.h>
#include <string.h>

#include "board.h"
#include "firmware_version.h"
#include "serial_bridge.h"
#include "target_swd.h"

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

#define DAP_INFO_VENDOR           0x01U
#define DAP_INFO_PRODUCT          0x02U
#define DAP_INFO_SERIAL           0x03U
#define DAP_INFO_FW_VERSION       0x04U
#define DAP_INFO_PRODUCT_FW_VERSION 0x09U
#define DAP_INFO_CAPABILITIES     0xF0U
#define DAP_INFO_PACKET_COUNT     0xFEU
#define DAP_INFO_PACKET_SIZE      0xFFU

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
#define DAP_VENDOR_STATUS_VERSION 5U

typedef enum {
    DAP_STATE_IDLE = 0,
    DAP_STATE_CONNECT,
    DAP_STATE_TRANSFER,
    DAP_STATE_RESET,
    DAP_STATE_COMMAND,
    DAP_STATE_PINS,
    DAP_STATE_SWD_SEQUENCE
} dap_state_t;

static uint8_t s_request[CMSIS_DAP_PACKET_SIZE];
static uint8_t s_response[CMSIS_DAP_PACKET_SIZE];
static swd_tunnel_transfer_t s_transfers[DAP_MAX_TRANSFERS];
static dap_state_t s_state;
static uint8_t s_request_length;
static uint8_t s_response_length;
static uint8_t s_transfer_count;
static uint8_t s_transfer_done;
static uint8_t s_chunk_count;
static uint8_t s_transaction_id;
static uint32_t s_deadline;
static bool s_response_ready;
static bool s_connected;
static bool s_transfer_block;
static bool s_write_abort;
static uint8_t s_idle_cycles;
static uint16_t s_retry_count;
static uint16_t s_match_retry;
static uint8_t s_turnaround;
static bool s_data_phase;
static volatile bool s_abort_requested;

static void response_finish(uint8_t length);

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

static void command_vendor_status(void)
{
    serial_bridge_status_t status;
    uint8_t flags = 0U;

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
    response_finish(47U);
}

static void response_finish(uint8_t length)
{
    s_response_length = length;
    s_response_ready = true;
    s_state = DAP_STATE_IDLE;
}

static void response_error(uint8_t command)
{
    s_response[0] = command;
    s_response[1] = DAP_ERROR;
    response_finish(2U);
}

static void response_invalid(void)
{
    s_response[0] = 0xFFU;
    response_finish(1U);
}

static void info_string(const char *text)
{
    uint8_t length = (uint8_t)strlen(text);

    if (length > CMSIS_DAP_PACKET_SIZE - 3U) {
        length = CMSIS_DAP_PACKET_SIZE - 3U;
    }
    s_response[1] = (uint8_t)(length + 1U);
    memcpy(&s_response[2], text, length);
    s_response[2U + length] = '\0';
    response_finish((uint8_t)(3U + length));
}

static void command_info(void)
{
    uint8_t info_id;

    if (s_request_length < 2U) {
        response_error(DAP_INFO);
        return;
    }
    s_response[0] = DAP_INFO;
    info_id = s_request[1];
    if (info_id == DAP_INFO_VENDOR) {
        info_string("DAPLink");
    } else if (info_id == DAP_INFO_PRODUCT) {
        info_string("DAPLink-Wireless");
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
        info_string(serial);
    } else if ((info_id == DAP_INFO_FW_VERSION) ||
               (info_id == DAP_INFO_PRODUCT_FW_VERSION)) {
        info_string(FIRMWARE_VERSION_STRING);
    } else if (info_id == DAP_INFO_CAPABILITIES) {
        s_response[1] = 2U;
        s_response[2] = 0x01U;
        s_response[3] = 0x01U;
        response_finish(4U);
    } else if (info_id == DAP_INFO_PACKET_COUNT) {
        s_response[1] = 1U;
        s_response[2] = 1U;
        response_finish(3U);
    } else if (info_id == DAP_INFO_PACKET_SIZE) {
        s_response[1] = 2U;
        s_response[2] = CMSIS_DAP_PACKET_SIZE;
        s_response[3] = 0U;
        response_finish(4U);
    } else {
        s_response[1] = 0U;
        response_finish(2U);
    }
}

static void operation_start(dap_state_t state)
{
    s_state = state;
    s_deadline = board_millis() + DAP_OPERATION_TIMEOUT_MS;
}

static void command_connect(void)
{
    uint8_t port = s_request_length >= 2U ? s_request[1] : 0U;

    s_response[0] = DAP_CONNECT;
    if ((port != 0U) && (port != DAP_PORT_SWD)) {
        s_response[1] = DAP_PORT_DISABLED;
        response_finish(2U);
        return;
    }
    if (!serial_bridge_swd_connect(++s_transaction_id)) {
        s_response[1] = DAP_PORT_DISABLED;
        response_finish(2U);
        return;
    }
    operation_start(DAP_STATE_CONNECT);
}

static void command_disconnect(void)
{
    s_response[0] = DAP_DISCONNECT;
    s_connected = false;
    if (!serial_bridge_swd_disconnect(++s_transaction_id)) {
        s_response[1] = DAP_OK;
        response_finish(2U);
        return;
    }
    operation_start(DAP_STATE_COMMAND);
}

static void command_transfer_configure(void)
{
    if (s_request_length < 6U) {
        response_error(DAP_TRANSFER_CONFIGURE);
        return;
    }
    s_idle_cycles = s_request[1];
    s_retry_count = (uint16_t)s_request[2] |
                    ((uint16_t)s_request[3] << 8);
    s_match_retry = (uint16_t)s_request[4] |
                    ((uint16_t)s_request[5] << 8);
    s_response[0] = DAP_TRANSFER_CONFIGURE;
    if (!serial_bridge_swd_configure(
            ++s_transaction_id, s_idle_cycles, s_retry_count,
            s_match_retry,
            s_turnaround, s_data_phase)) {
        s_response[1] = DAP_ERROR;
        response_finish(2U);
        return;
    }
    operation_start(DAP_STATE_COMMAND);
}

static void command_swd_configure(void)
{
    uint8_t value;

    if (s_request_length < 2U) {
        response_error(DAP_SWD_CONFIGURE);
        return;
    }
    value = s_request[1];
    s_turnaround = (uint8_t)((value & 0x03U) + 1U);
    s_data_phase = (value & 0x04U) != 0U;
    s_response[0] = DAP_SWD_CONFIGURE;
    if (!serial_bridge_swd_configure(
            ++s_transaction_id, s_idle_cycles, s_retry_count,
            s_match_retry,
            s_turnaround, s_data_phase)) {
        s_response[1] = DAP_ERROR;
        response_finish(2U);
        return;
    }
    operation_start(DAP_STATE_COMMAND);
}

static bool transfer_parse(void)
{
    uint8_t count;
    uint8_t input_offset = 3U;
    uint8_t index;
    uint8_t read_count = 0U;

    if (s_request_length < 3U) {
        return false;
    }
    count = s_request[2];
    if ((count == 0U) || (count > DAP_MAX_TRANSFERS)) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        uint8_t request;

        if (input_offset >= s_request_length) {
            return false;
        }
        request = s_request[input_offset++];
        if ((request & DAP_TRANSFER_UNSUPPORTED) != 0U) {
            return false;
        }
        s_transfers[index].request = request & 0x3FU;
        s_transfers[index].data = 0U;
        if (((request & DAP_TRANSFER_RNW) == 0U) ||
            ((request & DAP_TRANSFER_MATCH_VALUE) != 0U)) {
            if ((uint8_t)(s_request_length - input_offset) < 4U) {
                return false;
            }
            s_transfers[index].data =
                decode_u32_le(&s_request[input_offset]);
            input_offset = (uint8_t)(input_offset + 4U);
        } else if (++read_count > 15U) {
            return false;
        }
    }
    s_transfer_count = count;
    s_transfer_done = 0U;
    s_transfer_block = false;
    s_write_abort = false;
    s_response[0] = DAP_TRANSFER;
    s_response[1] = 0U;
    s_response[2] = 0U;
    s_response_length = 3U;
    return true;
}

static bool transfer_block_parse(void)
{
    uint16_t count;
    uint8_t request;
    uint8_t input_offset = 5U;
    uint8_t index;

    if (s_request_length < 5U) {
        return false;
    }
    count = (uint16_t)s_request[2] |
            ((uint16_t)s_request[3] << 8);
    request = s_request[4];
    if ((count == 0U) || (count > DAP_MAX_TRANSFERS) ||
        ((request & 0xF0U) != 0U) ||
        (((request & DAP_TRANSFER_RNW) != 0U) && (count > 15U))) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        s_transfers[index].request = request & 0x0FU;
        s_transfers[index].data = 0U;
        if ((request & DAP_TRANSFER_RNW) == 0U) {
            if ((uint8_t)(s_request_length - input_offset) < 4U) {
                return false;
            }
            s_transfers[index].data =
                decode_u32_le(&s_request[input_offset]);
            input_offset = (uint8_t)(input_offset + 4U);
        }
    }
    s_transfer_count = (uint8_t)count;
    s_transfer_done = 0U;
    s_transfer_block = true;
    s_write_abort = false;
    s_response[0] = DAP_TRANSFER_BLOCK;
    s_response[1] = 0U;
    s_response[2] = 0U;
    s_response[3] = 0U;
    s_response_length = 4U;
    return true;
}

static bool transfer_chunk_submit(void)
{
    uint8_t remaining =
        (uint8_t)(s_transfer_count - s_transfer_done);

    s_chunk_count = remaining > SWD_TUNNEL_MAX_TRANSFERS
                        ? SWD_TUNNEL_MAX_TRANSFERS
                        : remaining;
    if (!serial_bridge_swd_transfers(
            ++s_transaction_id, &s_transfers[s_transfer_done],
            s_chunk_count)) {
        return false;
    }
    s_deadline = board_millis() + DAP_OPERATION_TIMEOUT_MS;
    return true;
}

static void command_transfer(void)
{
    if (!s_connected || !transfer_parse() || !transfer_chunk_submit()) {
        s_response[0] = DAP_TRANSFER;
        s_response[1] = 0U;
        s_response[2] = DAP_TRANSFER_ERROR;
        response_finish(3U);
        return;
    }
    operation_start(DAP_STATE_TRANSFER);
}

static void command_transfer_block(void)
{
    if (!s_connected || !transfer_block_parse() ||
        !transfer_chunk_submit()) {
        s_response[0] = DAP_TRANSFER_BLOCK;
        s_response[1] = 0U;
        s_response[2] = 0U;
        s_response[3] = DAP_TRANSFER_ERROR;
        response_finish(4U);
        return;
    }
    operation_start(DAP_STATE_TRANSFER);
}

static void command_write_abort(void)
{
    if (!s_connected || (s_request_length < 6U)) {
        response_error(DAP_WRITE_ABORT);
        return;
    }
    s_transfers[0].request = 0U;
    s_transfers[0].data = decode_u32_le(&s_request[2]);
    s_transfer_count = 1U;
    s_transfer_done = 0U;
    s_chunk_count = 1U;
    s_transfer_block = false;
    s_write_abort = true;
    s_response[0] = DAP_WRITE_ABORT;
    s_response_length = 2U;
    if (!serial_bridge_swd_transfers(++s_transaction_id,
                                     s_transfers, 1U)) {
        s_response[1] = DAP_ERROR;
        response_finish(2U);
        return;
    }
    operation_start(DAP_STATE_TRANSFER);
}

static void command_reset(void)
{
    s_response[0] = DAP_RESET_TARGET;
    if (!serial_bridge_swd_reset(++s_transaction_id)) {
        s_response[1] = DAP_ERROR;
        s_response[2] = 0U;
        response_finish(3U);
        return;
    }
    operation_start(DAP_STATE_RESET);
}

static void command_swj_clock(void)
{
    s_response[0] = DAP_SWJ_CLOCK;
    if ((s_request_length < 5U) ||
        !serial_bridge_swd_clock(++s_transaction_id,
                                 decode_u32_le(&s_request[1]))) {
        s_response[1] = DAP_ERROR;
        response_finish(2U);
        return;
    }
    operation_start(DAP_STATE_COMMAND);
}

static void command_swj_pins(void)
{
    uint32_t wait_us;

    s_response[0] = DAP_SWJ_PINS;
    if (s_request_length < 7U) {
        s_response[1] = 0U;
        response_finish(2U);
        return;
    }
    wait_us = decode_u32_le(&s_request[3]);
    if (wait_us > 3000000U) {
        wait_us = 3000000U;
    }
    if (!serial_bridge_swd_pins(++s_transaction_id, s_request[1],
                                s_request[2], wait_us)) {
        s_response[1] = 0U;
        response_finish(2U);
        return;
    }
    operation_start(DAP_STATE_PINS);
}

static void command_swj_sequence(void)
{
    uint16_t bit_count;
    uint8_t byte_count;

    s_response[0] = DAP_SWJ_SEQUENCE;
    if (s_request_length < 3U) {
        s_response[1] = DAP_ERROR;
        response_finish(2U);
        return;
    }
    bit_count = s_request[1] == 0U ? 256U : s_request[1];
    byte_count = (uint8_t)((bit_count + 7U) / 8U);
    if ((s_request_length < (uint8_t)(2U + byte_count)) ||
        !serial_bridge_swd_sequence(++s_transaction_id, bit_count,
                                    &s_request[2])) {
        s_response[1] = DAP_ERROR;
        response_finish(2U);
        return;
    }
    operation_start(DAP_STATE_COMMAND);
}

static void command_swd_sequence(void)
{
    uint8_t input_offset = 2U;
    uint8_t response_length = 1U;
    uint8_t sequence;

    s_response[0] = DAP_SWD_SEQUENCE;
    if (s_request_length < 2U) {
        s_response[1] = DAP_ERROR;
        response_finish(2U);
        return;
    }
    for (sequence = 0U; sequence < s_request[1]; ++sequence) {
        uint8_t info;
        uint8_t bit_count;
        uint8_t byte_count;

        if (input_offset >= s_request_length) {
            s_response[1] = DAP_ERROR;
            response_finish(2U);
            return;
        }
        info = s_request[input_offset++];
        bit_count = info & 0x3FU;
        if (bit_count == 0U) {
            bit_count = 64U;
        }
        byte_count = (uint8_t)((bit_count + 7U) / 8U);
        if ((info & 0x80U) != 0U) {
            response_length =
                (uint8_t)(response_length + byte_count);
            if (response_length >= CMSIS_DAP_PACKET_SIZE) {
                s_response[1] = DAP_ERROR;
                response_finish(2U);
                return;
            }
        } else {
            if ((uint8_t)(s_request_length - input_offset) <
                byte_count) {
                s_response[1] = DAP_ERROR;
                response_finish(2U);
                return;
            }
            input_offset = (uint8_t)(input_offset + byte_count);
        }
    }
    if (!serial_bridge_swd_sequence_io(
            ++s_transaction_id, &s_request[1],
            (uint8_t)(input_offset - 1U))) {
        s_response[1] = DAP_ERROR;
        response_finish(2U);
        return;
    }
    operation_start(DAP_STATE_SWD_SEQUENCE);
}

static void command_dispatch(void)
{
    uint8_t command = s_request[0];

    switch (command) {
    case DAP_INFO:
        command_info();
        break;
    case DAP_HOST_STATUS:
        if ((s_request_length < 3U) || (s_request[1] > 1U) ||
            (s_request[2] > 1U)) {
            response_error(command);
            break;
        }
        s_response[0] = command;
        s_response[1] = DAP_OK;
        response_finish(2U);
        break;
    case DAP_TRANSFER_CONFIGURE:
        command_transfer_configure();
        break;
    case DAP_CONNECT:
        command_connect();
        break;
    case DAP_DISCONNECT:
        command_disconnect();
        break;
    case DAP_TRANSFER:
        command_transfer();
        break;
    case DAP_TRANSFER_BLOCK:
        command_transfer_block();
        break;
    case DAP_TRANSFER_ABORT:
        response_error(command);
        break;
    case DAP_WRITE_ABORT:
        command_write_abort();
        break;
    case DAP_DELAY:
        s_response[0] = command;
        if (s_request_length < 3U) {
            s_response[1] = DAP_ERROR;
        } else {
            board_delay_us((uint16_t)s_request[1] |
                           ((uint16_t)s_request[2] << 8));
            s_response[1] = DAP_OK;
        }
        response_finish(2U);
        break;
    case DAP_RESET_TARGET:
        command_reset();
        break;
    case DAP_SWJ_PINS:
        command_swj_pins();
        break;
    case DAP_SWJ_CLOCK:
        command_swj_clock();
        break;
    case DAP_SWJ_SEQUENCE:
        command_swj_sequence();
        break;
    case DAP_SWD_CONFIGURE:
        command_swd_configure();
        break;
    case DAP_SWD_SEQUENCE:
        command_swd_sequence();
        break;
    case DAP_VENDOR_STATUS:
        command_vendor_status();
        break;
    default:
        response_invalid();
        break;
    }
}

static void connect_complete(const swd_tunnel_response_t *result)
{
    s_connected = result->ack == TARGET_SWD_ACK_OK;
    s_response[1] = s_connected ? DAP_PORT_SWD : DAP_PORT_DISABLED;
    response_finish(2U);
}

static void transfer_complete(const swd_tunnel_response_t *result)
{
    uint8_t index;

    if (s_write_abort) {
        s_response[1] =
            result->ack == TARGET_SWD_ACK_OK ? DAP_OK : DAP_ERROR;
        response_finish(2U);
        return;
    }
    for (index = 0U; index < result->completed; ++index) {
        uint8_t transfer_index = (uint8_t)(s_transfer_done + index);

        if ((s_transfers[transfer_index].request &
             DAP_TRANSFER_RNW) != 0U &&
            (s_transfers[transfer_index].request &
             DAP_TRANSFER_MATCH_VALUE) == 0U) {
            if (s_response_length >
                CMSIS_DAP_PACKET_SIZE - 4U) {
                if (s_transfer_block) {
                    s_response[3] = DAP_TRANSFER_ERROR;
                } else {
                    s_response[2] = DAP_TRANSFER_ERROR;
                }
                response_finish(s_response_length);
                return;
            }
            encode_u32_le(&s_response[s_response_length],
                          result->data[index]);
            s_response_length =
                (uint8_t)(s_response_length + 4U);
        }
    }
    s_transfer_done =
        (uint8_t)(s_transfer_done + result->completed);
    if (s_transfer_block) {
        s_response[1] = s_transfer_done;
        s_response[2] = 0U;
        s_response[3] = result->ack;
    } else {
        s_response[1] = s_transfer_done;
        s_response[2] = result->ack;
    }
    if ((result->ack != TARGET_SWD_ACK_OK) ||
        (result->completed != s_chunk_count) ||
        (s_transfer_done == s_transfer_count)) {
        response_finish(s_response_length);
        return;
    }
    if (!transfer_chunk_submit()) {
        if (s_transfer_block) {
            s_response[3] = DAP_TRANSFER_ERROR;
        } else {
            s_response[2] = DAP_TRANSFER_ERROR;
        }
        response_finish(s_response_length);
    }
}

void cmsis_dap_init(void)
{
    s_state = DAP_STATE_IDLE;
    s_response_ready = false;
    s_connected = false;
    s_transaction_id = 0U;
    s_idle_cycles = 0U;
    s_retry_count = 100U;
    s_match_retry = 0U;
    s_turnaround = 1U;
    s_data_phase = false;
    s_abort_requested = false;
}

bool cmsis_dap_submit(const uint8_t *request, uint8_t length)
{
    if ((request == NULL) || (length == 0U) ||
        (length > sizeof(s_request)) ||
        (s_state != DAP_STATE_IDLE) || s_response_ready) {
        return false;
    }
    memcpy(s_request, request, length);
    s_request_length = length;
    command_dispatch();
    return true;
}

void cmsis_dap_abort(void)
{
    s_abort_requested = true;
    target_swd_abort_request();
}

void cmsis_dap_process(void)
{
    swd_tunnel_response_t result;

    if ((s_state == DAP_STATE_IDLE) || s_response_ready) {
        s_abort_requested = false;
        return;
    }
    if (s_abort_requested) {
        s_abort_requested = false;
        if (s_state == DAP_STATE_TRANSFER) {
            serial_bridge_swd_cancel(s_transaction_id);
            if (s_write_abort) {
                s_response[1] = DAP_ERROR;
            } else if (s_transfer_block) {
                s_response[3] = DAP_TRANSFER_ERROR;
            } else {
                s_response[2] = DAP_TRANSFER_ERROR;
            }
            response_finish(s_response_length);
            return;
        }
    }
    if (serial_bridge_swd_response_take(&result)) {
        if (result.transaction_id != s_transaction_id) {
            return;
        }
        if (s_state == DAP_STATE_CONNECT) {
            if (result.operation != SWD_TUNNEL_OP_CONNECT) {
                s_connected = false;
                s_response[1] = DAP_PORT_DISABLED;
                response_finish(2U);
                return;
            }
            connect_complete(&result);
        } else if (s_state == DAP_STATE_TRANSFER) {
            if ((result.operation != SWD_TUNNEL_OP_TRANSFER) ||
                (result.completed > s_chunk_count) ||
                (result.completed >
                 (uint8_t)(s_transfer_count - s_transfer_done))) {
                if (s_write_abort) {
                    s_response[1] = DAP_ERROR;
                } else if (s_transfer_block) {
                    s_response[3] = DAP_TRANSFER_ERROR;
                } else {
                    s_response[2] = DAP_TRANSFER_ERROR;
                }
                response_finish(s_response_length);
                return;
            }
            transfer_complete(&result);
        } else if (s_state == DAP_STATE_RESET) {
            if (result.operation != SWD_TUNNEL_OP_RESET) {
                s_response[1] = DAP_ERROR;
                s_response[2] = 0U;
                response_finish(3U);
                return;
            }
            s_response[1] =
                result.ack == TARGET_SWD_ACK_OK ? DAP_OK : DAP_ERROR;
            s_response[2] = 1U;
            response_finish(3U);
        } else if (s_state == DAP_STATE_COMMAND) {
            uint8_t expected_operation = s_response[0] == DAP_DISCONNECT
                                             ? SWD_TUNNEL_OP_DISCONNECT
                                         : s_response[0] ==
                                                   DAP_TRANSFER_CONFIGURE ||
                                                   s_response[0] ==
                                                       DAP_SWD_CONFIGURE
                                             ? SWD_TUNNEL_OP_CONFIGURE
                                         : s_response[0] == DAP_SWJ_CLOCK
                                             ? SWD_TUNNEL_OP_CLOCK
                                             : SWD_TUNNEL_OP_SEQUENCE;

            if (result.operation != expected_operation) {
                s_response[1] = s_response[0] == DAP_DISCONNECT
                                    ? DAP_OK
                                    : DAP_ERROR;
                response_finish(2U);
                return;
            }
            s_response[1] = s_response[0] == DAP_DISCONNECT
                                ? DAP_OK
                                : result.ack == TARGET_SWD_ACK_OK
                                      ? DAP_OK
                                      : DAP_ERROR;
            response_finish(2U);
        } else if (s_state == DAP_STATE_PINS) {
            if (result.operation != SWD_TUNNEL_OP_PINS) {
                s_response[1] = 0U;
                response_finish(2U);
                return;
            }
            s_response[1] =
                result.completed != 0U ? (uint8_t)result.data[0] : 0U;
            response_finish(2U);
        } else if (s_state == DAP_STATE_SWD_SEQUENCE) {
            if ((result.operation != SWD_TUNNEL_OP_SWD_SEQUENCE) ||
                (result.raw_length == 0U) ||
                (result.raw_length >= CMSIS_DAP_PACKET_SIZE)) {
                s_response[1] = DAP_ERROR;
                response_finish(2U);
            } else {
                memcpy(&s_response[1], result.raw,
                       result.raw_length);
                response_finish(
                    (uint8_t)(1U + result.raw_length));
            }
        }
        return;
    }
    if ((int32_t)(board_millis() - s_deadline) >= 0) {
        serial_bridge_swd_cancel(s_transaction_id);
        if (s_state == DAP_STATE_CONNECT) {
            s_connected = false;
            s_response[1] = DAP_PORT_DISABLED;
            response_finish(2U);
        } else if (s_state == DAP_STATE_TRANSFER) {
            if (s_write_abort) {
                s_response[1] = DAP_ERROR;
            } else if (s_transfer_block) {
                s_response[3] = DAP_TRANSFER_ERROR;
            } else {
                s_response[2] = DAP_TRANSFER_ERROR;
            }
            response_finish(s_response_length);
        } else if (s_state == DAP_STATE_RESET) {
            s_response[1] = DAP_ERROR;
            s_response[2] = 0U;
            response_finish(3U);
        } else {
            s_response[1] = s_response[0] == DAP_DISCONNECT
                                ? DAP_OK
                                : DAP_ERROR;
            response_finish(2U);
        }
    }
}

bool cmsis_dap_busy(void)
{
    return (s_state != DAP_STATE_IDLE) || s_response_ready;
}

bool cmsis_dap_response_take(uint8_t *response, uint8_t *length)
{
    if ((response == NULL) || (length == NULL) || !s_response_ready) {
        return false;
    }
    *length = s_response_length;
    memcpy(response, s_response, s_response_length);
    s_response_ready = false;
    return true;
}
