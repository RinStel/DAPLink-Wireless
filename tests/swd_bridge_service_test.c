#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "swd_bridge_service.h"
#include "target_swd.h"

static bool s_tunnel_request_active;
static bool s_tunnel_response_ready;
static uint8_t s_transaction_id;

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
    return 0;
}
