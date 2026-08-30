#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "swd_bridge_service.h"
#include "target_swd.h"

static bool s_tunnel_request_active;
static bool s_tunnel_response_ready;
static uint8_t s_transaction_id;
static uint8_t s_submit_count;
static uint8_t s_submitted_transactions[SWD_TUNNEL_BURST_MAX_BLOCKS];

bool swd_tunnel_burst_decode(const uint8_t *payload, uint8_t length,
                             swd_tunnel_burst_t *burst)
{
    if ((payload == NULL) || (length != 3U) || (burst == NULL) ||
        (payload[0] != SWD_TUNNEL_OP_BURST)) {
        return false;
    }
    memset(burst, 0, sizeof(*burst));
    burst->transaction_id = payload[1];
    burst->count = 2U;
    burst->blocks[0].transaction_id = 7U;
    burst->blocks[0].count = 1U;
    burst->blocks[0].transfers[0].request = 0x02U;
    burst->blocks[1].transaction_id = 8U;
    burst->blocks[1].count = 1U;
    burst->blocks[1].transfers[0].request = 0x00U;
    burst->blocks[1].transfers[0].data = 0x11223344U;
    return true;
}

bool swd_tunnel_block_encoded_lengths(const swd_tunnel_block_t *block,
                                      uint8_t *request_length,
                                      uint8_t *worst_response_length)
{
    if ((block == NULL) || (request_length == NULL) ||
        (worst_response_length == NULL) || (block->count != 1U)) {
        return false;
    }
    *request_length = (block->transfers[0].request & 0x02U) != 0U
                          ? 3U
                          : 7U;
    *worst_response_length = (block->transfers[0].request & 0x02U) != 0U
                                 ? 8U
                                 : 4U;
    return true;
}

uint8_t swd_tunnel_burst_response_encode(
    const swd_tunnel_burst_response_t *response, uint8_t *payload)
{
    if ((response == NULL) || (payload == NULL) ||
        (response->count != 2U)) {
        return 0U;
    }
    payload[0] = SWD_TUNNEL_OP_BURST;
    payload[1] = response->transaction_id;
    payload[2] = response->count;
    return 3U;
}

bool swd_tunnel_burst_response_decode(
    const uint8_t *payload, uint8_t length,
    swd_tunnel_burst_response_t *response)
{
    if ((payload == NULL) || (response == NULL) || (length != 3U) ||
        (payload[0] != SWD_TUNNEL_OP_BURST)) {
        return false;
    }
    memset(response, 0, sizeof(*response));
    response->transaction_id = payload[1];
    response->count = payload[2];
    return true;
}

bool swd_tunnel_decode_block(const uint8_t *payload, uint8_t length,
                             swd_tunnel_block_t *block)
{
    if ((payload == NULL) || (length != 3U) || (block == NULL)) {
        return false;
    }
    memset(block, 0, sizeof(*block));
    block->transaction_id = payload[0];
    block->count = 1U;
    block->transfers[0].request = payload[2];
    return true;
}

bool swd_tunnel_submit_block(uint8_t transaction_id,
                             const swd_tunnel_transfer_t *transfers,
                             uint8_t count)
{
    if ((transfers == NULL) || (count != 1U)) {
        return false;
    }
    s_transaction_id = transaction_id;
    s_submitted_transactions[s_submit_count++] = transaction_id;
    s_tunnel_request_active = true;
    s_tunnel_response_ready = false;
    return true;
}

bool swd_tunnel_submit(const uint8_t *request, uint8_t request_length)
{
    (void)request;
    (void)request_length;
    return false;
}

bool swd_tunnel_decode_block_response(
    const uint8_t *payload, uint8_t length,
    swd_tunnel_block_response_t *response)
{
    (void)payload;
    (void)length;
    (void)response;
    return false;
}

void swd_tunnel_process_budget(uint32_t batch_budget_us)
{
    if (s_tunnel_request_active && (batch_budget_us >= 1600U)) {
        s_tunnel_request_active = false;
        s_tunnel_response_ready = true;
    }
}

bool swd_tunnel_response_take(uint8_t *response, uint8_t *length)
{
    if ((response == NULL) || (length == NULL) ||
        !s_tunnel_response_ready) {
        return false;
    }
    response[0] = SWD_TUNNEL_OP_BLOCK;
    response[1] = s_transaction_id;
    response[2] = 1U;
    response[3] = TARGET_SWD_ACK_OK;
    *length = 4U;
    s_tunnel_response_ready = false;
    return true;
}

bool swd_tunnel_decode_response(const uint8_t *payload, uint8_t length,
                                swd_tunnel_response_t *response)
{
    if ((payload == NULL) || (length != 4U) || (response == NULL)) {
        return false;
    }
    memset(response, 0, sizeof(*response));
    response->operation = payload[0];
    response->transaction_id = payload[1];
    response->completed = payload[2];
    response->ack = (target_swd_ack_t)payload[3];
    return true;
}

uint8_t swd_tunnel_encode_block_response(
    uint8_t transaction_id, uint8_t completed, uint8_t ack,
    const uint32_t *read_data, uint8_t read_count, uint8_t *payload)
{
    (void)read_data;
    (void)read_count;
    payload[0] = transaction_id;
    payload[1] = completed;
    payload[2] = (uint8_t)ack;
    payload[3] = 0U;
    return 4U;
}

void swd_tunnel_cancel(void)
{
    s_tunnel_request_active = false;
    s_tunnel_response_ready = false;
}

int main(void)
{
    const uint8_t request[] = {7U, 1U, 0x02U};
    uint8_t reply[SWD_TUNNEL_MAX_PAYLOAD];
    uint8_t reply_length = 0U;

    swd_bridge_service_init();
    assert(!swd_bridge_service_busy());
    assert(swd_bridge_service_wireless_block_request(
        request, sizeof(request)));
    assert(swd_bridge_service_busy());

    /* 无线从机使用扩大的批处理预算。该请求应在一次服务调用中完成。 */
    swd_bridge_service_process();
    assert(swd_bridge_service_reply_take(reply, &reply_length));
    assert(reply_length == 4U);
    assert(reply[0] == 7U);
    assert(!swd_bridge_service_busy());

    {
        const uint8_t burst_request[] = {
            SWD_TUNNEL_OP_BURST, 0x70U, 2U
        };

        s_submit_count = 0U;
        assert(swd_bridge_service_wireless_burst_request(
            burst_request, sizeof(burst_request)));
        assert(s_submit_count == 1U);
        assert(s_submitted_transactions[0] == 7U);
        swd_bridge_service_process();
        assert(s_submit_count == 2U);
        assert(s_submitted_transactions[1] == 8U);
        assert(!swd_bridge_service_reply_take(reply, &reply_length));
        swd_bridge_service_process();
        assert(swd_bridge_service_reply_is_burst());
        assert(swd_bridge_service_reply_take(reply, &reply_length));
        assert(reply_length == 3U);
        assert(reply[0] == SWD_TUNNEL_OP_BURST);
        assert(reply[1] == 0x70U);

        swd_bridge_service_reset();
        s_submit_count = 0U;
        assert(swd_bridge_service_wireless_burst_request(
            burst_request, sizeof(burst_request)));
        assert(s_submit_count == 1U);
        assert(swd_bridge_service_wireless_abort(0x70U));
        assert(s_submit_count == 1U);
        assert(swd_bridge_service_reply_is_burst());
        assert(swd_bridge_service_reply_take(reply, &reply_length));
        assert(reply[0] == SWD_TUNNEL_OP_BURST);
        assert(reply[1] == 0x70U);
    }
    return 0;
}
