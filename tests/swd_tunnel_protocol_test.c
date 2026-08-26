#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "swd_tunnel.h"
#include "target_swd.h"

/* 目标 SWD 替身使主机上的线格式、响应布局和取消测试具有确定性。 */
static bool s_sequence_transfer_called;
static bool s_cancel_during_transfer;
static bool s_abort_requested;
static uint32_t s_now_ms;
static uint32_t s_transfer_advance_ms;
static uint32_t s_transfer_calls;
static bool s_async_transfer;
static uint32_t *s_async_data;
static uint16_t s_async_waits_remaining;
static uint8_t s_async_requests[8];
static uint32_t s_async_results[8];
static uint8_t s_async_request_count;
static uint8_t s_async_result_count;

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

bool target_swd_transfer_begin(uint8_t request, uint32_t *data)
{
    if (s_async_request_count < sizeof(s_async_requests)) {
        s_async_requests[s_async_request_count++] = request;
    }
    s_async_transfer = true;
    s_async_data = data;
    return true;
}

target_swd_poll_result_t target_swd_transfer_poll(target_swd_ack_t *ack)
{
    if (!s_async_transfer) {
        return TARGET_SWD_POLL_IDLE;
    }
    s_async_transfer = false;
    ++s_transfer_calls;
    s_now_ms += s_transfer_advance_ms;
    if (s_async_waits_remaining != 0U) {
        --s_async_waits_remaining;
        *ack = TARGET_SWD_ACK_WAIT;
        return TARGET_SWD_POLL_DONE;
    }
    if (s_cancel_during_transfer) {
        swd_tunnel_cancel();
        *ack = TARGET_SWD_ACK_WAIT;
        return TARGET_SWD_POLL_CANCELLED;
    }
    if (s_async_data != NULL) {
        uint8_t result_index = (uint8_t)(s_transfer_calls - 1U);

        *s_async_data = result_index < s_async_result_count
                            ? s_async_results[result_index]
                            : 0U;
    }
    *ack = TARGET_SWD_ACK_OK;
    return TARGET_SWD_POLL_DONE;
}

void target_swd_transfer_cancel(void)
{
    s_async_transfer = false;
    s_abort_requested = true;
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
    uint8_t sequence_data[SWD_TUNNEL_MAX_PAYLOAD - 4U] = {0U};
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
    swd_tunnel_block_t block;
    swd_tunnel_block_response_t block_response;
    uint32_t block_data[SWD_TUNNEL_MAX_BLOCK_TRANSFERS] = {
        0x01020304U, 0xA0B0C0D0U
    };
    const uint8_t raw_response[] = {
        SWD_TUNNEL_OP_SWD_SEQUENCE, 7U, 2U, 0U, 0U, 0xA5U
    };

    assert(swd_tunnel_encode_transfers(3U, &transfer, 1U, payload) == 8U);
    assert(payload[3] == 0x32U);
    assert(payload[4] == 0x78U);
    assert(payload[7] == 0x12U);

    {
        swd_tunnel_transfer_t block_transfers[3] = {
            {.request = 0x02U, .data = 0U},
            {.request = 0x00U, .data = 0x11223344U},
            {.request = 0x12U, .data = 0U}
        };

        length = swd_tunnel_encode_block(
            8U, block_transfers, 3U, payload);
        assert(length == 2U + 3U + 4U);
        assert(swd_tunnel_decode_block(payload, length, &block));
        assert(block.transaction_id == 8U);
        assert(block.count == 3U);
        assert(block.transfers[0].request == 0x02U);
        assert(block.transfers[1].request == 0x00U);
        assert(block.transfers[1].data == 0x11223344U);
        assert(block.transfers[2].request == 0x12U);
        assert(swd_tunnel_encode_block(
                   8U, block_transfers,
                   (uint8_t)(SWD_TUNNEL_MAX_BLOCK_TRANSFERS + 1U),
                   payload) == 0U);

        assert(swd_tunnel_encode_block_response(
                   8U, 3U, TARGET_SWD_ACK_OK, block_data, 2U,
                   payload) == 12U);
        assert(swd_tunnel_decode_block_response(
            payload, 12U, &block_response));
        assert(block_response.transaction_id == 8U);
        assert(block_response.completed == 3U);
        assert(block_response.ack == TARGET_SWD_ACK_OK);
        assert(block_response.read_count == 2U);
        assert(block_response.data[0] == block_data[0]);
        assert(block_response.data[1] == block_data[1]);
        assert(!swd_tunnel_decode_block(payload, (uint8_t)(length - 1U),
                                        &block));
    }

    assert(swd_tunnel_encode_sequence(
               6U, 480U, sequence_data, payload) == 64U);
    assert(payload[2] == 0xE0U);
    assert(payload[3] == 0x01U);
    assert(swd_tunnel_encode_sequence(
               6U, 481U, sequence_data, payload) == 0U);
    assert(swd_tunnel_encode_sequence(
               6U, UINT16_MAX, sequence_data, payload) == 0U);

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
        while (!swd_tunnel_response_take(payload, &length)) {
            swd_tunnel_process();
        }
        assert(s_transfer_calls == 1U);
        assert(payload[3] == 0x11U);
    }

    {
        swd_tunnel_transfer_t wait_transfer = {
            .request = 0x02U,
            .data = 0U
        };

        s_async_waits_remaining = 2U;
        s_transfer_calls = 0U;
        length = swd_tunnel_encode_transfers(
            15U, &wait_transfer, 1U, payload);
        assert(swd_tunnel_submit(payload, length));
        while (!swd_tunnel_response_take(payload, &length)) {
            swd_tunnel_process();
        }
        assert(s_transfer_calls == 3U);
        assert(payload[3] == TARGET_SWD_ACK_OK);
    }

    {
        swd_tunnel_transfer_t ap_idr_read = {
            .request = 0x0FU,
            .data = 0U
        };

        s_transfer_calls = 0U;
        s_async_request_count = 0U;
        s_async_result_count = 2U;
        s_async_results[0] = 0U;
        s_async_results[1] = 0x14770011U;
        length = swd_tunnel_encode_transfers(
            16U, &ap_idr_read, 1U, payload);
        assert(swd_tunnel_submit(payload, length));
        while (!swd_tunnel_response_take(payload, &length)) {
            swd_tunnel_process();
        }
        assert(swd_tunnel_decode_response(payload, length, &response));
        assert(s_transfer_calls == 2U);
        assert(s_async_request_count == 2U);
        assert(s_async_requests[0] == 0x0FU);
        assert(s_async_requests[1] == 0x0EU);
        assert(response.completed == 1U);
        assert(response.ack == TARGET_SWD_ACK_OK);
        assert(response.data[0] == 0x14770011U);
    }

    {
        swd_tunnel_transfer_t ap_reads[2] = {
            {.request = 0x0FU, .data = 0U},
            {.request = 0x0FU, .data = 0U}
        };

        s_transfer_calls = 0U;
        s_async_request_count = 0U;
        s_async_result_count = 3U;
        s_async_results[0] = 0U;
        s_async_results[1] = 0x11111111U;
        s_async_results[2] = 0x22222222U;
        length = swd_tunnel_encode_transfers(
            17U, ap_reads, 2U, payload);
        assert(swd_tunnel_submit(payload, length));
        while (!swd_tunnel_response_take(payload, &length)) {
            swd_tunnel_process();
        }
        assert(swd_tunnel_decode_response(payload, length, &response));
        assert(s_transfer_calls == 3U);
        assert(s_async_requests[0] == 0x0FU);
        assert(s_async_requests[1] == 0x0FU);
        assert(s_async_requests[2] == 0x0EU);
        assert(response.completed == 2U);
        assert(response.data[0] == 0x11111111U);
        assert(response.data[1] == 0x22222222U);
    }

    {
        swd_tunnel_transfer_t final_write = {
            .request = 0x00U,
            .data = 0xA5A5A5A5U
        };

        s_transfer_calls = 0U;
        s_async_request_count = 0U;
        s_async_result_count = 2U;
        length = swd_tunnel_encode_transfers(
            18U, &final_write, 1U, payload);
        assert(swd_tunnel_submit(payload, length));
        while (!swd_tunnel_response_take(payload, &length)) {
            swd_tunnel_process();
        }
        assert(swd_tunnel_decode_response(payload, length, &response));
        assert(s_transfer_calls == 2U);
        assert(s_async_requests[0] == 0x00U);
        assert(s_async_requests[1] == 0x0EU);
        assert(response.completed == 1U);
        assert(response.ack == TARGET_SWD_ACK_OK);
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

    memset(payload, 0, sizeof(payload));
    payload[0] = SWD_TUNNEL_OP_SEQUENCE;
    payload[1] = 11U;
    payload[2] = 0xFFU;
    payload[3] = 0xFFU;
    assert(!swd_tunnel_submit(payload, 5U));

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
    swd_tunnel_process();
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
        while (!swd_tunnel_response_take(payload, &length)) {
            swd_tunnel_process();
        }
        assert(s_transfer_calls < 20U);
        assert(payload[3] == 0x11U);
        s_transfer_advance_ms = 0U;
    }
    return 0;
}
