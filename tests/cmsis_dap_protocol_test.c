#include <assert.h>
#include <string.h>

#include "cmsis_dap.h"
#include "firmware_version.h"
#include "serial_bridge.h"
#include "target_swd.h"

#define DAP_INFO               0x00U
#define DAP_CONNECT            0x02U
#define DAP_DISCONNECT         0x03U
#define DAP_TRANSFER_CONFIGURE 0x04U
#define DAP_TRANSFER           0x05U
#define DAP_RESET_TARGET       0x0AU
#define DAP_SWD_SEQUENCE       0x1DU

static uint32_t s_now_ms;
static uint8_t s_transaction;
static bool s_response_available;
static bool s_cancelled;
static swd_tunnel_response_t s_bridge_response;
static swd_tunnel_transfer_t s_captured_transfers[16];
static uint8_t s_captured_transfer_count;
static uint8_t s_sequence_request[62];
static uint8_t s_sequence_length;

uint32_t board_millis(void)
{
    return s_now_ms;
}

void board_delay_us(uint32_t delay_us)
{
    (void)delay_us;
}

uint32_t board_device_id_hash(void)
{
    return 0x1234ABCDU;
}

uint8_t board_reset_cause(void)
{
    return 0U;
}

void target_swd_abort_request(void)
{
}

bool serial_bridge_swd_connect(uint8_t transaction_id)
{
    s_transaction = transaction_id;
    return true;
}

bool serial_bridge_swd_disconnect(uint8_t transaction_id)
{
    s_transaction = transaction_id;
    return true;
}

bool serial_bridge_swd_reset(uint8_t transaction_id)
{
    s_transaction = transaction_id;
    return true;
}

bool serial_bridge_swd_sequence(uint8_t transaction_id,
                                uint16_t bit_count,
                                const uint8_t *data)
{
    (void)bit_count;
    (void)data;
    s_transaction = transaction_id;
    return true;
}

bool serial_bridge_swd_sequence_io(uint8_t transaction_id,
                                   const uint8_t *request,
                                   uint8_t request_length)
{
    s_transaction = transaction_id;
    s_sequence_length = request_length;
    memcpy(s_sequence_request, request, request_length);
    return true;
}

bool serial_bridge_swd_clock(uint8_t transaction_id, uint32_t clock_hz)
{
    (void)clock_hz;
    s_transaction = transaction_id;
    return true;
}

bool serial_bridge_swd_configure(uint8_t transaction_id,
                                 uint8_t idle_cycles,
                                 uint16_t retry_count,
                                 uint16_t match_retry,
                                 uint8_t turnaround,
                                 bool data_phase)
{
    (void)idle_cycles;
    (void)retry_count;
    (void)match_retry;
    (void)turnaround;
    (void)data_phase;
    s_transaction = transaction_id;
    return true;
}

bool serial_bridge_swd_pins(uint8_t transaction_id, uint8_t value,
                            uint8_t select, uint32_t wait_us)
{
    (void)value;
    (void)select;
    (void)wait_us;
    s_transaction = transaction_id;
    return true;
}

bool serial_bridge_swd_transfers(
    uint8_t transaction_id, const swd_tunnel_transfer_t *transfers,
    uint8_t count)
{
    s_transaction = transaction_id;
    s_captured_transfer_count = count;
    memcpy(s_captured_transfers, transfers,
           count * sizeof(s_captured_transfers[0]));
    return true;
}

void serial_bridge_swd_cancel(uint8_t transaction_id)
{
    assert(transaction_id == s_transaction);
    s_cancelled = true;
}

bool serial_bridge_swd_response_take(swd_tunnel_response_t *response)
{
    if (!s_response_available) {
        return false;
    }
    *response = s_bridge_response;
    s_response_available = false;
    return true;
}

void serial_bridge_status_get(serial_bridge_status_t *status)
{
    memset(status, 0, sizeof(*status));
}

static void bridge_complete(uint8_t operation, uint8_t completed,
                            uint8_t ack)
{
    memset(&s_bridge_response, 0, sizeof(s_bridge_response));
    s_bridge_response.operation = operation;
    s_bridge_response.transaction_id = s_transaction;
    s_bridge_response.completed = completed;
    s_bridge_response.ack = ack;
    s_response_available = true;
    cmsis_dap_process();
}

static uint8_t response_take(uint8_t *response)
{
    uint8_t length = 0U;

    assert(cmsis_dap_response_take(response, &length));
    return length;
}

int main(void)
{
    uint8_t request[CMSIS_DAP_PACKET_SIZE] = {0U};
    uint8_t response[CMSIS_DAP_PACKET_SIZE];
    uint8_t length;

    cmsis_dap_init();

    request[0] = DAP_RESET_TARGET;
    assert(cmsis_dap_submit(request, 1U));
    bridge_complete(SWD_TUNNEL_OP_RESET, 0U, TARGET_SWD_ACK_OK);
    assert(response_take(response) == 3U);
    assert(response[0] == DAP_RESET_TARGET);
    assert(response[1] == 0U);
    assert(response[2] == 1U);

    request[0] = DAP_INFO;
    request[1] = 0x04U;
    assert(cmsis_dap_submit(request, sizeof(request)));
    length = response_take(response);
    assert(length == (uint8_t)(sizeof(FIRMWARE_VERSION_STRING) + 2U));
    assert(response[0] == DAP_INFO);
    assert(response[1] == sizeof(FIRMWARE_VERSION_STRING));
    assert(strcmp((char *)&response[2], FIRMWARE_VERSION_STRING) == 0);

    request[0] = DAP_INFO;
    request[1] = 0xF0U;
    assert(cmsis_dap_submit(request, 2U));
    assert(response_take(response) == 4U);
    assert(response[1] == 2U);
    assert(response[2] == 0x01U);
    assert(response[3] == 0x01U);

    request[0] = DAP_INFO;
    request[1] = 0x09U;
    assert(cmsis_dap_submit(request, 2U));
    assert(response_take(response) ==
           (uint8_t)(sizeof(FIRMWARE_VERSION_STRING) + 2U));
    assert(strcmp((char *)&response[2], FIRMWARE_VERSION_STRING) == 0);

    request[0] = 0x55U;
    assert(cmsis_dap_submit(request, 1U));
    assert(response_take(response) == 1U);
    assert(response[0] == 0xFFU);

    request[0] = DAP_CONNECT;
    request[1] = 1U;
    assert(cmsis_dap_submit(request, 2U));
    bridge_complete(SWD_TUNNEL_OP_CONNECT, 0U, TARGET_SWD_ACK_OK);
    assert(response_take(response) == 2U);
    assert(response[0] == DAP_CONNECT);
    assert(response[1] == 1U);

    request[0] = DAP_DISCONNECT;
    assert(cmsis_dap_submit(request, 1U));
    s_now_ms = 5000U;
    cmsis_dap_process();
    assert(s_cancelled);
    assert(response_take(response) == 2U);
    assert(response[0] == DAP_DISCONNECT);
    assert(response[1] == 0U);

    request[0] = DAP_CONNECT;
    request[1] = 1U;
    assert(cmsis_dap_submit(request, 2U));
    bridge_complete(SWD_TUNNEL_OP_CONNECT, 0U, TARGET_SWD_ACK_OK);
    assert(response_take(response) == 2U);
    assert(response[1] == 1U);

    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER_CONFIGURE;
    request[1] = 8U;
    request[2] = 100U;
    request[4] = 3U;
    assert(cmsis_dap_submit(request, sizeof(request)));
    bridge_complete(SWD_TUNNEL_OP_CONFIGURE, 0U, TARGET_SWD_ACK_OK);
    assert(response_take(response) == 2U);
    assert(response[1] == 0U);

    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER;
    request[2] = 1U;
    request[3] = 0x02U;
    assert(cmsis_dap_submit(request, 4U));
    assert(s_captured_transfer_count == 1U);
    assert(s_captured_transfers[0].request == 0x02U);
    memset(&s_bridge_response, 0, sizeof(s_bridge_response));
    s_bridge_response.operation = SWD_TUNNEL_OP_TRANSFER;
    s_bridge_response.transaction_id = s_transaction;
    s_bridge_response.completed = 1U;
    s_bridge_response.ack = TARGET_SWD_ACK_OK;
    s_bridge_response.data[0] = 0x78563412U;
    s_response_available = true;
    cmsis_dap_process();
    length = response_take(response);
    assert(length == 7U);
    assert(response[1] == 1U);
    assert(response[2] == TARGET_SWD_ACK_OK);
    assert(response[3] == 0x12U);
    assert(response[6] == 0x78U);

    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER;
    request[2] = 12U;
    for (uint8_t index = 0U; index < 12U; ++index) {
        uint8_t offset = (uint8_t)(3U + index * 5U);

        request[offset] = 0x00U;
        request[offset + 1U] = index;
    }
    assert(cmsis_dap_submit(request, 63U));
    assert(s_captured_transfer_count == 10U);
    bridge_complete(SWD_TUNNEL_OP_TRANSFER, 10U, TARGET_SWD_ACK_OK);
    assert(s_captured_transfer_count == 2U);
    bridge_complete(SWD_TUNNEL_OP_TRANSFER, 2U, TARGET_SWD_ACK_OK);
    assert(response_take(response) == 3U);
    assert(response[1] == 12U);
    assert(response[2] == TARGET_SWD_ACK_OK);

    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER;
    request[2] = 12U;
    for (uint8_t index = 0U; index < 12U; ++index) {
        request[3U + index * 5U] = 0x00U;
    }
    assert(cmsis_dap_submit(request, 63U));
    bridge_complete(SWD_TUNNEL_OP_TRANSFER, 10U, TARGET_SWD_ACK_OK);
    assert(s_captured_transfer_count == 2U);
    bridge_complete(SWD_TUNNEL_OP_TRANSFER, 10U, TARGET_SWD_ACK_OK);
    assert(response_take(response) == 3U);
    assert(response[1] == 10U);
    assert(response[2] == 0x08U);

    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER;
    request[2] = 2U;
    request[3] = 0x20U;
    request[4] = 0xFFU;
    request[8] = 0x12U;
    request[9] = 0x34U;
    assert(cmsis_dap_submit(request, 13U));
    assert(s_captured_transfer_count == 2U);
    assert(s_captured_transfers[0].request == 0x20U);
    assert(s_captured_transfers[1].request == 0x12U);
    bridge_complete(SWD_TUNNEL_OP_TRANSFER, 2U, TARGET_SWD_ACK_OK);
    assert(response_take(response) == 3U);
    assert(response[1] == 2U);

    memset(request, 0, sizeof(request));
    request[0] = DAP_SWD_SEQUENCE;
    request[1] = 1U;
    request[2] = 0x88U;
    assert(cmsis_dap_submit(request, 3U));
    assert(s_sequence_length == 2U);
    assert(s_sequence_request[0] == 1U);
    assert(s_sequence_request[1] == 0x88U);
    memset(&s_bridge_response, 0, sizeof(s_bridge_response));
    s_bridge_response.operation = SWD_TUNNEL_OP_SWD_SEQUENCE;
    s_bridge_response.transaction_id = s_transaction;
    s_bridge_response.ack = TARGET_SWD_ACK_OK;
    s_bridge_response.raw_length = 2U;
    s_bridge_response.raw[0] = 0U;
    s_bridge_response.raw[1] = 0xA5U;
    s_response_available = true;
    cmsis_dap_process();
    assert(response_take(response) == 3U);
    assert(response[0] == DAP_SWD_SEQUENCE);
    assert(response[1] == 0U);
    assert(response[2] == 0xA5U);

    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER;
    request[2] = 1U;
    request[3] = 0x02U;
    assert(cmsis_dap_submit(request, 4U));
    s_cancelled = false;
    cmsis_dap_abort();
    cmsis_dap_process();
    assert(s_cancelled);
    assert(response_take(response) == 3U);
    assert(response[2] == 0x08U);

    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER;
    request[2] = 1U;
    request[3] = 0x82U;
    assert(cmsis_dap_submit(request, 4U));
    assert(response_take(response) == 3U);
    assert(response[2] == 0x08U);

    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER;
    request[2] = 1U;
    request[3] = 0x02U;
    s_now_ms = 100U;
    assert(cmsis_dap_submit(request, 4U));
    s_cancelled = false;
    s_now_ms = 5000U;
    cmsis_dap_process();
    assert(s_cancelled);
    assert(response_take(response) == 3U);
    assert(response[2] == 0x08U);

    return 0;
}
