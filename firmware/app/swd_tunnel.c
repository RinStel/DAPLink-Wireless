#include "swd_tunnel.h"

#include <stddef.h>
#include <string.h>

#include "board.h"
#include "target_swd.h"

#define SWD_TUNNEL_RESPONSE_HEADER_SIZE 4U
#define SWD_TRANSFER_MATCH_VALUE        0x10U
#define SWD_TRANSFER_MATCH_MASK         0x20U
#define SWD_TRANSFER_MISMATCH           0x10U
#define SWD_TUNNEL_MAX_MATCH_RETRIES    128U

static uint8_t s_response[SWD_TUNNEL_MAX_PAYLOAD];
static uint8_t s_response_length;
static uint8_t s_pending_value;
static uint8_t s_pending_select;
static uint8_t s_pending_transaction;
static uint32_t s_pending_deadline_cycles;
static bool s_pending;
static bool s_response_ready;
static uint32_t s_match_mask;
static uint16_t s_match_retry;

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
    uint8_t byte_count = (uint8_t)((bit_count + 7U) / 8U);

    if ((payload == NULL) || (data == NULL) || (bit_count == 0U) ||
        (byte_count > SWD_TUNNEL_MAX_PAYLOAD - 4U)) {
        return 0U;
    }
    payload[0] = SWD_TUNNEL_OP_SEQUENCE;
    payload[1] = transaction_id;
    payload[2] = (uint8_t)bit_count;
    payload[3] = (uint8_t)(bit_count >> 8);
    memcpy(&payload[4], data, byte_count);
    return (uint8_t)(4U + byte_count);
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
        byte_count = (uint8_t)((bit_count + 7U) / 8U);
        if ((bit_count == 0U) ||
            (request_length != (uint8_t)(4U + byte_count)) ||
            !target_swd_sequence(bit_count, &request[4])) {
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
                         (attempts <= s_match_retry));
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

bool swd_tunnel_submit(const uint8_t *request, uint8_t request_length)
{
    uint32_t wait_us;
    uint8_t pins;

    if ((request == NULL) || (request_length < 2U) ||
        s_pending || s_response_ready) {
        return false;
    }
    if (request[0] != SWD_TUNNEL_OP_PINS) {
        if (!execute_immediate(request, request_length, s_response,
                               &s_response_length)) {
            return false;
        }
        s_response_ready = true;
        return true;
    }
    if (request_length != 8U) {
        return false;
    }
    target_swd_pins_set(request[2], request[3]);
    pins = target_swd_pins_read();
    wait_us = decode_u32_le(&request[4]);
    if ((wait_us == 0U) ||
        (((pins ^ request[2]) & request[3] & 0x83U) == 0U)) {
        s_response[0] = SWD_TUNNEL_OP_PINS;
        s_response[1] = request[1];
        s_response[2] = 1U;
        s_response[3] = (uint8_t)TARGET_SWD_ACK_OK;
        encode_u32_le(&s_response[SWD_TUNNEL_RESPONSE_HEADER_SIZE],
                      pins);
        s_response_length = SWD_TUNNEL_RESPONSE_HEADER_SIZE + 4U;
        s_response_ready = true;
        return true;
    }
    s_pending_value = request[2];
    s_pending_select = request[3];
    s_pending_transaction = request[1];
    s_pending_deadline_cycles =
        board_cycle_count() + board_cycles_from_us(wait_us);
    s_pending = true;
    return true;
}

void swd_tunnel_process(void)
{
    uint8_t pins;

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
    s_pending = false;
    s_response_ready = false;
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
