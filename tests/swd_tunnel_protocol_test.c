#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "swd_tunnel.h"
#include "target_swd.h"

static bool s_sequence_transfer_called;
static bool s_cancel_during_transfer;
static bool s_abort_requested;
static uint32_t s_now_ms;
static uint32_t s_transfer_advance_ms;
static uint32_t s_transfer_calls;

uint32_t board_millis(void)
{
    return s_now_ms;
}

uint32_t board_cycle_count(void)
{
    return 0U;
}

uint32_t board_cycles_from_us(uint32_t delay_us)
{
    return delay_us;
}

void target_swd_init(uint32_t clock_hz)
{
    (void)clock_hz;
}

void target_swd_configure(uint8_t idle_cycles, uint16_t retry_count,
                          uint8_t turnaround, bool data_phase)
{
    (void)idle_cycles;
    (void)retry_count;
    (void)turnaround;
    (void)data_phase;
}

void target_swd_disconnect(void)
{
}

target_swd_ack_t target_swd_transfer(uint8_t request, uint32_t *data)
{
    (void)request;
    ++s_transfer_calls;
    s_now_ms += s_transfer_advance_ms;
    if (s_cancel_during_transfer) {
        swd_tunnel_cancel();
        return TARGET_SWD_ACK_WAIT;
    }
    if (data != NULL) {
        *data = 0U;
    }
    return TARGET_SWD_ACK_OK;
}

void target_swd_abort_clear(void)
{
}

void target_swd_abort_request(void)
{
    s_abort_requested = true;
}

bool target_swd_sequence(uint16_t bit_count, const uint8_t *data)
{
    (void)bit_count;
    (void)data;
    return true;
}

bool target_swd_sequence_transfer(const uint8_t *request,
                                  uint8_t request_length,
                                  uint8_t *response,
                                  uint8_t *response_length)
{
    (void)request;
    (void)request_length;
    s_sequence_transfer_called = true;
    response[0] = 0U;
    *response_length = 1U;
    return true;
}

void target_swd_pins_set(uint8_t value, uint8_t select)
{
    (void)value;
    (void)select;
}

uint8_t target_swd_pins_read(void)
{
    return 0U;
}

void target_swd_reset_pulse(uint32_t duration_ms)
{
    (void)duration_ms;
}

int main(void)
{
    uint8_t payload[SWD_TUNNEL_MAX_PAYLOAD];
    uint8_t length;
    swd_tunnel_transfer_t transfer = {
        .request = 0x32U,
        .data = 0x12345678U
    };
    swd_tunnel_transfer_t cancel_transfer = {
        .request = 0x02U,
        .data = 0U
    };
    swd_tunnel_response_t response;
    const uint8_t raw_response[] = {
        SWD_TUNNEL_OP_SWD_SEQUENCE, 7U, 2U, 0U, 0U, 0xA5U
    };

    assert(swd_tunnel_encode_transfers(3U, &transfer, 1U, payload) == 8U);
    assert(payload[3] == 0x32U);
    assert(payload[4] == 0x78U);
    assert(payload[7] == 0x12U);

    assert(swd_tunnel_encode_configure(
               4U, 2U, 0x1234U, 0x5678U, 1U, false, payload) == 9U);
    assert(payload[3] == 0x34U);
    assert(payload[4] == 0x12U);
    assert(payload[5] == 0x78U);
    assert(payload[6] == 0x56U);

    assert(swd_tunnel_submit(payload, 9U));
    swd_tunnel_process();
    assert(swd_tunnel_response_take(payload, &length));
    assert(length == 4U);

    {
        swd_tunnel_transfer_t match_transfers[2] = {
            {.request = 0x20U, .data = 0xFFFFFFFFU},
            {.request = 0x12U, .data = 1U}
        };

        length = swd_tunnel_encode_transfers(
            5U, match_transfers, 2U, payload);
        s_transfer_calls = 0U;
        assert(swd_tunnel_submit(payload, length));
        swd_tunnel_process();
        assert(swd_tunnel_response_take(payload, &length));
        assert(s_transfer_calls == 129U);
        assert(payload[3] == 0x11U);
    }

    assert(swd_tunnel_decode_response(
        raw_response, sizeof(raw_response), &response));
    assert(response.operation == SWD_TUNNEL_OP_SWD_SEQUENCE);
    assert(response.transaction_id == 7U);
    assert(response.raw_length == 2U);
    assert(response.raw[0] == 0U);
    assert(response.raw[1] == 0xA5U);

    memset(payload, 0x5A, sizeof(payload));
    for (length = 0U; length < sizeof(payload); ++length) {
        (void)swd_tunnel_decode_response(payload, length, &response);
    }

    memset(payload, 0x80, sizeof(payload));
    payload[0] = SWD_TUNNEL_OP_SWD_SEQUENCE;
    payload[1] = 9U;
    payload[2] = 61U;
    s_sequence_transfer_called = false;
    assert(!swd_tunnel_submit(payload, sizeof(payload)));
    assert(!s_sequence_transfer_called);

    payload[0] = SWD_TUNNEL_OP_SWD_SEQUENCE;
    payload[1] = 10U;
    payload[2] = 1U;
    payload[3] = 0x88U;
    payload[4] = 0xAAU;
    s_sequence_transfer_called = false;
    assert(!swd_tunnel_submit(payload, 5U));
    assert(!s_sequence_transfer_called);

    length = swd_tunnel_encode_transfers(
        11U, &cancel_transfer, 1U, payload);
    s_transfer_calls = 0U;
    assert(swd_tunnel_submit(payload, length));
    swd_tunnel_cancel();
    swd_tunnel_process();
    assert(s_transfer_calls == 0U);
    assert(!swd_tunnel_response_take(payload, &length));

    length = swd_tunnel_encode_transfers(
        12U, &cancel_transfer, 1U, payload);
    s_cancel_during_transfer = true;
    s_abort_requested = false;
    assert(swd_tunnel_submit(payload, length));
    swd_tunnel_process();
    s_cancel_during_transfer = false;
    assert(s_abort_requested);
    assert(!swd_tunnel_response_take(payload, &length));

    cancel_transfer.request = 0x22U;
    length = swd_tunnel_encode_transfers(
        13U, &cancel_transfer, 1U, payload);
    assert(!swd_tunnel_submit(payload, length));

    {
        swd_tunnel_transfer_t budget_transfers[2] = {
            {.request = 0x20U, .data = 0xFFFFFFFFU},
            {.request = 0x12U, .data = 1U}
        };

        s_now_ms = 0U;
        s_transfer_advance_ms = 300U;
        s_transfer_calls = 0U;
        length = swd_tunnel_encode_transfers(
            14U, budget_transfers, 2U, payload);
        assert(swd_tunnel_submit(payload, length));
        swd_tunnel_process();
        assert(swd_tunnel_response_take(payload, &length));
        assert(s_transfer_calls < 20U);
        assert(payload[3] == 0x11U);
        s_transfer_advance_ms = 0U;
    }
    return 0;
}
