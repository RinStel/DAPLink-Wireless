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
static uint32_t s_cycle_count;
static uint32_t s_cycle_advance;
static uint32_t s_transfer_advance_ms;
static uint32_t s_transfer_calls;
static bool s_async_transfer;
static uint32_t *s_async_data;
static uint16_t s_async_waits_remaining;
static uint8_t s_async_requests[140];
static uint32_t s_async_results[140];
static uint8_t s_async_request_count;
static uint8_t s_async_result_count;

uint32_t board_millis(void)
{
    return s_now_ms;
}

uint32_t board_cycle_count(void)
{
    uint32_t result = s_cycle_count;

    s_cycle_count += s_cycle_advance;
    return result;
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
    swd_tunnel_transfer_t cancel_transfer = {
        .request = 0x02U,
        .data = 0U
    };
    swd_tunnel_response_t response;
    swd_tunnel_block_t block;
    swd_tunnel_block_response_t block_response;
    swd_tunnel_burst_t burst;
    swd_tunnel_burst_t decoded_burst;
    swd_tunnel_burst_response_t burst_response;
    swd_tunnel_burst_response_t decoded_burst_response;
    uint32_t block_data[SWD_TUNNEL_MAX_BLOCK_TRANSFERS] = {
        0x01020304U, 0xA0B0C0D0U
    };
    const uint8_t raw_response[] = {
        SWD_TUNNEL_OP_SWD_SEQUENCE, 7U, 2U, 0U, 0U, 0xA5U
    };

    {
        swd_tunnel_transfer_t first_transfers[1] = {
            {.request = 0x02U, .data = 0U}
        };
        swd_tunnel_transfer_t second_transfers[1] = {
            {.request = 0x01U, .data = 0x11223344U}
        };

        memset(&burst, 0, sizeof(burst));
        burst.transaction_id = 0x71U;
        burst.count = 2U;
        burst.blocks[0].transaction_id = 0x21U;
        burst.blocks[0].count = 1U;
        burst.blocks[0].transfers[0] = first_transfers[0];
        burst.blocks[1].transaction_id = 0x22U;
        burst.blocks[1].count = 1U;
        burst.blocks[1].transfers[0] = second_transfers[0];

        length = swd_tunnel_burst_encode(&burst, payload);
        assert(length == 15U);
        assert(payload[0] == SWD_TUNNEL_OP_BURST);
        assert(swd_tunnel_burst_decode(payload, length, &decoded_burst));
        assert(decoded_burst.transaction_id == 0x71U);
        assert(decoded_burst.count == 2U);
        assert(decoded_burst.blocks[0].transaction_id == 0x21U);
        assert(decoded_burst.blocks[0].count == 1U);
        assert(decoded_burst.blocks[0].transfers[0].request == 0x02U);
        assert(decoded_burst.blocks[1].transaction_id == 0x22U);
        assert(decoded_burst.blocks[1].count == 1U);
        assert(decoded_burst.blocks[1].transfers[0].request == 0x01U);
        assert(decoded_burst.blocks[1].transfers[0].data == 0x11223344U);
    }

    {
        uint32_t first_data[1] = {0x10203040U};
        uint32_t second_data[2] = {0x55667788U, 0xAABBCCDDU};

        memset(&burst_response, 0, sizeof(burst_response));
        burst_response.transaction_id = 0x71U;
        burst_response.count = 2U;
        burst_response.responses[0].transaction_id = 0x21U;
        burst_response.responses[0].completed = 1U;
        burst_response.responses[0].ack = TARGET_SWD_ACK_OK;
        burst_response.responses[0].read_count = 1U;
        burst_response.responses[0].data[0] = first_data[0];
        burst_response.responses[1].transaction_id = 0x22U;
        burst_response.responses[1].completed = 2U;
        burst_response.responses[1].ack = TARGET_SWD_ACK_FAULT;
        burst_response.responses[1].read_count = 2U;
        burst_response.responses[1].data[0] = second_data[0];
        burst_response.responses[1].data[1] = second_data[1];

        length = swd_tunnel_burst_response_encode(&burst_response, payload);
        assert(length == 25U);
        assert(swd_tunnel_burst_response_decode(
            payload, length, &decoded_burst_response));
        assert(decoded_burst_response.transaction_id == 0x71U);
        assert(decoded_burst_response.count == 2U);
        assert(decoded_burst_response.responses[0].data[0] == first_data[0]);
        assert(decoded_burst_response.responses[1].ack ==
               TARGET_SWD_ACK_FAULT);
        assert(decoded_burst_response.responses[1].data[1] == second_data[1]);
        assert(!swd_tunnel_burst_response_decode(
            payload, (uint8_t)(length - 1U), &decoded_burst_response));
        payload[2] = 3U;
        assert(!swd_tunnel_burst_response_decode(
            payload, length, &decoded_burst_response));
    }

    {
        uint8_t index;

        memset(&burst_response, 0, sizeof(burst_response));
        burst_response.transaction_id = 0x72U;
        burst_response.count = 3U;
        for (index = 0U; index < 3U; ++index) {
            burst_response.responses[index].transaction_id = index;
            burst_response.responses[index].completed = 9U;
            burst_response.responses[index].ack = TARGET_SWD_ACK_OK;
            burst_response.responses[index].read_count = 9U;
        }
        assert(swd_tunnel_burst_response_encode(
                   &burst_response, payload) == 0U);
    }

    {
        swd_tunnel_transfer_t block_transfers[3] = {
            {.request = 0x02U, .data = 0U},
            {.request = 0x00U, .data = 0x11223344U},
            {.request = 0x12U, .data = 0xA1B2C3D4U}
        };

        length = swd_tunnel_encode_block(
            8U, block_transfers, 3U, payload);
        /* Arm DAP_Transfer 的 Match Value 读请求也携带 4 字节
         * 期望值；无线 block 必须与写请求一样保留该载荷。 */
        assert(length == 13U);
        assert(payload[9] == 0xD4U);
        assert(payload[10] == 0xC3U);
        assert(payload[11] == 0xB2U);
        assert(payload[12] == 0xA1U);
        assert(swd_tunnel_decode_block(payload, length, &block));
        assert(block.transaction_id == 8U);
        assert(block.count == 3U);
        assert(block.transfers[0].request == 0x02U);
        assert(block.transfers[1].request == 0x00U);
        assert(block.transfers[1].data == 0x11223344U);
        assert(block.transfers[2].request == 0x12U);
        assert(block.transfers[2].data == 0xA1B2C3D4U);
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

    /* 运行时配置两个 Match Value 重试；总尝试次数为首次读取加两次重试。 */
    assert(swd_tunnel_encode_configure(
               4U, 2U, 0x1234U, 2U, 1U, false, payload) == 9U);
    assert(swd_tunnel_submit(payload, 9U));
    swd_tunnel_process();
    assert(swd_tunnel_response_take(payload, &length));
    assert(length == 4U);

    {
        swd_tunnel_transfer_t match_transfers[2] = {
            {.request = 0x20U, .data = 0xFFFFFFFFU},
            {.request = 0x12U, .data = 1U}
        };

        s_transfer_calls = 0U;
        s_async_request_count = 0U;
        s_async_result_count = 0U;
        assert(swd_tunnel_submit_block(5U, match_transfers, 2U));
        while (!swd_tunnel_response_take(payload, &length)) {
            swd_tunnel_process();
        }
        assert(s_transfer_calls == 3U);
        /* Arm DAP_SWD_Transfer 在 Value Mismatch 分支跳出后不会执行
         * response_count++；失败的 Match Value 项不计入 completed。 */
        assert(payload[2] == 1U);
        assert(payload[3] == 0x11U);
    }

    {
        swd_tunnel_transfer_t match_transfers[2] = {
            {.request = 0x20U, .data = 0xFFFFFFFFU},
            {.request = 0x12U, .data = 1U}
        };

        /* DP Match Value 首次不匹配，第二次匹配时必须成功。 */
        s_transfer_calls = 0U;
        s_async_request_count = 0U;
        s_async_result_count = 2U;
        s_async_results[0] = 0U;
        s_async_results[1] = 1U;
        assert(swd_tunnel_submit_block(6U, match_transfers, 2U));
        while (!swd_tunnel_response_take(payload, &length)) {
            swd_tunnel_process();
        }
        assert(s_transfer_calls == 2U);
        assert(payload[2] == 2U);
        assert(payload[3] == TARGET_SWD_ACK_OK);
    }

    {
        swd_tunnel_transfer_t match_transfers[2] = {
            {.request = 0x20U, .data = 0xFFFFFFFFU},
            {.request = 0x12U, .data = 1U}
        };

        /* 16 位 match_retry 不得钳制为 128；129 次重试应允许第 130 次
         * 读取完成匹配。 */
        assert(swd_tunnel_encode_configure(
                   7U, 2U, 0x1234U, 129U, 1U, false,
                   payload) == 9U);
        assert(swd_tunnel_submit(payload, 9U));
        swd_tunnel_process();
        assert(swd_tunnel_response_take(payload, &length));
        s_transfer_calls = 0U;
        s_async_request_count = 0U;
        memset(s_async_results, 0, sizeof(s_async_results));
        s_async_result_count = 130U;
        s_async_results[129] = 1U;
        assert(swd_tunnel_submit_block(7U, match_transfers, 2U));
        while (!swd_tunnel_response_take(payload, &length)) {
            swd_tunnel_process();
        }
        assert(s_transfer_calls == 130U);
        assert(payload[2] == 2U);
        assert(payload[3] == TARGET_SWD_ACK_OK);
    }

    {
        swd_tunnel_transfer_t ap_match_transfers[2] = {
            {.request = 0x20U, .data = 0xFFFFFFFFU},
            {.request = 0x13U, .data = 0xA5U}
        };

        /* AP Match Value 先发一次 posted read；后续两次 AP read 分别返回
         * 不匹配值和匹配值。 */
        assert(swd_tunnel_encode_configure(
                   8U, 2U, 0x1234U, 2U, 1U, false,
                   payload) == 9U);
        assert(swd_tunnel_submit(payload, 9U));
        swd_tunnel_process();
        assert(swd_tunnel_response_take(payload, &length));
        s_transfer_calls = 0U;
        s_async_request_count = 0U;
        s_async_result_count = 3U;
        s_async_results[0] = 0xDEADBEEFU;
        s_async_results[1] = 0U;
        s_async_results[2] = 0xA5U;
        assert(swd_tunnel_submit_block(
            8U, ap_match_transfers, 2U));
        while (!swd_tunnel_response_take(payload, &length)) {
            swd_tunnel_process();
        }
        assert(s_transfer_calls == 3U);
        assert(s_async_request_count == 3U);
        assert(s_async_requests[0] == 0x13U);
        assert(s_async_requests[1] == 0x13U);
        assert(s_async_requests[2] == 0x13U);
        assert(payload[2] == 2U);
        assert(payload[3] == TARGET_SWD_ACK_OK);
    }

    {
        swd_tunnel_transfer_t wait_transfer = {
            .request = 0x02U,
            .data = 0U
        };

        s_async_waits_remaining = 2U;
        s_transfer_calls = 0U;
        assert(swd_tunnel_submit_block(15U, &wait_transfer, 1U));
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
        assert(swd_tunnel_submit_block(16U, &ap_idr_read, 1U));
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
        assert(swd_tunnel_submit_block(17U, ap_reads, 2U));
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
        assert(swd_tunnel_submit_block(18U, &final_write, 1U));
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

    {
        swd_tunnel_transfer_t block_reads[2] = {
            {.request = 0x0FU, .data = 0U},
            {.request = 0x0FU, .data = 0U}
        };

        s_transfer_calls = 0U;
        s_async_request_count = 0U;
        s_async_result_count = 3U;
        s_async_results[0] = 0U;
        s_async_results[1] = 0x33333333U;
        s_async_results[2] = 0x44444444U;
        uint8_t process_calls = 0U;

        assert(swd_tunnel_submit_block(19U, block_reads, 2U));
        while (!swd_tunnel_response_take(payload, &length)) {
            swd_tunnel_process();
            ++process_calls;
        }
        assert(process_calls <= 3U);
        assert(swd_tunnel_decode_response(payload, length, &response));
        assert(s_transfer_calls == 3U);
        assert(s_async_requests[0] == 0x0FU);
        assert(s_async_requests[1] == 0x0FU);
        assert(s_async_requests[2] == 0x0EU);
        assert(response.completed == 2U);
        assert(response.data[0] == 0x33333333U);
        assert(response.data[1] == 0x44444444U);
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

    s_transfer_calls = 0U;
    assert(swd_tunnel_submit_block(11U, &cancel_transfer, 1U));
    swd_tunnel_cancel();
    swd_tunnel_process();
    assert(s_transfer_calls == 0U);
    assert(!swd_tunnel_response_take(payload, &length));

    s_cancel_during_transfer = true;
    s_abort_requested = false;
    assert(swd_tunnel_submit_block(12U, &cancel_transfer, 1U));
    swd_tunnel_process();
    swd_tunnel_process();
    swd_tunnel_process();
    s_cancel_during_transfer = false;
    assert(s_abort_requested);
    assert(!swd_tunnel_response_take(payload, &length));

    cancel_transfer.request = 0x22U;
    assert(!swd_tunnel_submit_block(13U, &cancel_transfer, 1U));

    {
        swd_tunnel_transfer_t fast_batch[4] = {
            {.request = 0x00U, .data = 1U},
            {.request = 0x00U, .data = 2U},
            {.request = 0x00U, .data = 3U},
            {.request = 0x00U, .data = 4U}
        };

        s_transfer_calls = 0U;
        s_cycle_count = 0U;
        s_cycle_advance = 500U;
        assert(swd_tunnel_submit_block(20U, fast_batch, 4U));
        swd_tunnel_process_budget(1600U);
        assert(s_transfer_calls == 4U);
        while (!swd_tunnel_response_take(payload, &length)) {
            swd_tunnel_process_budget(1600U);
        }
        s_cycle_advance = 0U;
    }

    {
        swd_tunnel_transfer_t budget_transfers[2] = {
            {.request = 0x20U, .data = 0xFFFFFFFFU},
            {.request = 0x12U, .data = 1U}
        };

        assert(swd_tunnel_encode_configure(
                   14U, 2U, 0x1234U, 100U, 1U, false,
                   payload) == 9U);
        assert(swd_tunnel_submit(payload, 9U));
        swd_tunnel_process();
        assert(swd_tunnel_response_take(payload, &length));
        s_now_ms = 0U;
        s_transfer_advance_ms = 300U;
        s_transfer_calls = 0U;
        s_async_result_count = 0U;
        assert(swd_tunnel_submit_block(14U, budget_transfers, 2U));
        while (!swd_tunnel_response_take(payload, &length)) {
            swd_tunnel_process();
        }
        assert(s_transfer_calls < 20U);
        assert(payload[3] == TARGET_SWD_ACK_WAIT);
        s_transfer_advance_ms = 0U;
    }
    return 0;
}
