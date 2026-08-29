#include <assert.h>
#include <string.h>

#include "cmsis_dap.h"
#include "firmware_version.h"
#include "serial_bridge.h"
#include "target_swd.h"

/* 主机替身隔离 CMSIS-DAP 解析器与无线、UART 和 SWD 硬件。 */
#define DAP_INFO               0x00U
#define DAP_CONNECT            0x02U
#define DAP_DISCONNECT         0x03U
#define DAP_TRANSFER_CONFIGURE 0x04U
#define DAP_TRANSFER           0x05U
#define DAP_TRANSFER_BLOCK     0x06U
#define DAP_RESET_TARGET       0x0AU
#define DAP_DELAY              0x09U
#define DAP_SWD_SEQUENCE       0x1DU
#define DAP_EXECUTE_COMMANDS   0x7FU
#define DAP_VENDOR_TRACE       0x81U
#define CMSIS_DAP_PROTOCOL_VERSION "2.1.2"

static uint32_t s_now_ms;
static uint8_t s_transaction;
static bool s_response_available;
static bool s_cancelled;
static uint32_t s_pump_calls;
static bool s_pump_completes;
static bool s_cancel_complete;
static swd_tunnel_response_t s_bridge_response;
static swd_tunnel_transfer_t s_captured_transfers[16];
static uint8_t s_captured_transfer_count;
static uint8_t s_sequence_request[62];
static uint8_t s_sequence_length;

uint32_t board_millis(void)
{
    return s_now_ms;
}

uint32_t board_cycle_count(void)
{
    return s_now_ms * 120000U;
}

uint32_t board_cycles_from_us(uint32_t delay_us)
{
    return delay_us * 120U;
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

bool serial_bridge_swd_cancel_complete(uint8_t transaction_id)
{
    assert(transaction_id == s_transaction);
    return s_cancel_complete;
}

void serial_bridge_swd_pump(void)
{
    ++s_pump_calls;
    /* 模拟本地 SWD 引擎在 pump 期间完成 block：响应必须能在同一次
     * cmsis_dap_process() 内被取走。 */
    if (s_pump_completes) {
        s_pump_completes = false;
        memset(&s_bridge_response, 0, sizeof(s_bridge_response));
        s_bridge_response.operation = SWD_TUNNEL_OP_BLOCK;
        s_bridge_response.transaction_id = s_transaction;
        s_bridge_response.completed = 1U;
        s_bridge_response.ack = TARGET_SWD_ACK_OK;
        s_response_available = true;
    }
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
    request[1] = 0x01U;
    assert(cmsis_dap_submit(request, 2U));
    assert(response_take(response) == 10U);
    assert(response[1] == 8U);
    assert(strcmp((char *)&response[2], "RinStel") == 0);

    request[1] = 0x02U;
    assert(cmsis_dap_submit(request, 2U));
    assert(response_take(response) == 12U);
    assert(response[1] == 10U);
    assert(strcmp((char *)&response[2], "CMSIS-DAP") == 0);

    request[0] = DAP_INFO;
    request[1] = 0x04U;
    assert(cmsis_dap_submit(request, sizeof(request)));
    length = response_take(response);
    assert(length ==
           (uint8_t)(sizeof(CMSIS_DAP_PROTOCOL_VERSION) + 2U));
    assert(response[0] == DAP_INFO);
    assert(response[1] == sizeof(CMSIS_DAP_PROTOCOL_VERSION));
    assert(strcmp((char *)&response[2],
                  CMSIS_DAP_PROTOCOL_VERSION) == 0);

    request[0] = DAP_INFO;
    request[1] = 0xF0U;
    assert(cmsis_dap_submit(request, 2U));
    assert(response_take(response) == 4U);
    assert(response[1] == 2U);
    assert(response[2] == 0x01U);
    assert(response[3] == 0x01U);

    /* DAP_Delay 参数单位为毫秒，且处理必须异步完成。 */
    request[0] = DAP_DELAY;
    request[1] = 10U;
    request[2] = 0U;
    s_now_ms = 100U;
    assert(cmsis_dap_submit(request, 3U));
    cmsis_dap_process();
    assert(!s_response_available);
    s_now_ms = 109U;
    cmsis_dap_process();
    assert(!s_response_available);
    s_now_ms = 110U;
    cmsis_dap_process();
    assert(response_take(response) == 2U);
    assert(response[0] == DAP_DELAY && response[1] == 0U);

    request[0] = DAP_VENDOR_TRACE;
    request[1] = 0U;
    assert(cmsis_dap_submit(request, 2U));
    assert(response_take(response) == 3U);
    assert(response[0] == DAP_VENDOR_TRACE && response[1] == 1U &&
           response[2] == 0U);

    request[1] = 1U;
    request[2] = 0U;
    assert(cmsis_dap_submit(request, 3U));
    length = response_take(response);
    assert(length == 64U);
    assert(response[0] == DAP_VENDOR_TRACE && response[1] == 1U &&
           response[2] == 0U);

    request[0] = DAP_INFO;
    request[1] = 0x09U;
    assert(cmsis_dap_submit(request, 2U));
    assert(response_take(response) ==
           (uint8_t)(sizeof(FIRMWARE_VERSION_STRING) + 2U));
    assert(strcmp((char *)&response[2], FIRMWARE_VERSION_STRING) == 0);

    request[1] = 0xFEU;
    assert(cmsis_dap_submit(request, 2U));
    assert(response_take(response) == 3U);
    assert(response[0] == DAP_INFO);
    assert(response[1] == 1U);
    assert(response[2] == CMSIS_DAP_PACKET_COUNT);

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
    s_bridge_response.operation = SWD_TUNNEL_OP_BLOCK;
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

    /* A zero-count transfer is a valid no-op and must complete immediately. */
    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER;
    request[2] = 0U;
    assert(cmsis_dap_submit(request, 3U));
    assert(response_take(response) == 3U);
    assert(!cmsis_dap_busy());
    assert(response[1] == 0U && response[2] == 0U);

    /* DAP_TransferBlock has the same valid zero-count no-op semantics. */
    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER_BLOCK;
    request[2] = 0U;
    request[3] = 0U;
    request[4] = 0x02U;
    assert(cmsis_dap_submit(request, 5U));
    assert(response_take(response) == 4U);
    assert(!cmsis_dap_busy());
    assert(response[1] == 0U && response[2] == 0U && response[3] == 0U);

    /* Zero-count transfers must also be accepted inside ExecuteCommands. */
    memset(request, 0, sizeof(request));
    request[0] = DAP_EXECUTE_COMMANDS;
    request[1] = 1U;
    request[2] = DAP_TRANSFER;
    request[4] = 0U;
    assert(cmsis_dap_submit(request, 5U));
    assert(response_take(response) == 5U);
    assert(response[0] == DAP_EXECUTE_COMMANDS && response[1] == 1U);
    assert(response[2] == DAP_TRANSFER && response[3] == 0U && response[4] == 0U);

    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER;
    request[2] = 12U;
    for (uint8_t index = 0U; index < 12U; ++index) {
        uint8_t offset = (uint8_t)(3U + index * 5U);

        request[offset] = 0x00U;
        request[offset + 1U] = index;
    }
    assert(cmsis_dap_submit(request, 63U));
    assert(s_captured_transfer_count == 12U);
    bridge_complete(SWD_TUNNEL_OP_BLOCK, 12U, TARGET_SWD_ACK_OK);
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
    assert(s_captured_transfer_count == 12U);
    bridge_complete(SWD_TUNNEL_OP_BLOCK, 13U, TARGET_SWD_ACK_OK);
    assert(response_take(response) == 3U);
    assert(response[2] == 0x08U);

    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER;
    request[2] = 2U;
    request[3] = 0x20U;
    request[4] = 0xFFU;
    request[8] = 0x12U;
    request[9] = 0x34U;
    request[10] = 0x12U;
    request[11] = 0xCDU;
    request[12] = 0xABU;
    assert(cmsis_dap_submit(request, 13U));
    assert(s_captured_transfer_count == 2U);
    assert(s_captured_transfers[0].request == 0x20U);
    assert(s_captured_transfers[1].request == 0x12U);
    assert(s_captured_transfers[1].data == 0xABCD1234U);
    bridge_complete(SWD_TUNNEL_OP_BLOCK, 2U, TARGET_SWD_ACK_OK);
    assert(response_take(response) == 3U);
    assert(response[1] == 2U);

    /* Arm DAP_SWD_Transfer 不把触发 Value Mismatch 的项计入
     * Transfer Count，且不在响应中返回 Match Value 读数据。 */
    assert(cmsis_dap_submit(request, 13U));
    bridge_complete(SWD_TUNNEL_OP_BLOCK, 1U, 0x11U);
    assert(response_take(response) == 3U);
    assert(response[1] == 1U);
    assert(response[2] == 0x11U);

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
    s_cancel_complete = false;
    cmsis_dap_abort();
    cmsis_dap_process();
    assert(s_cancelled);
    assert(!cmsis_dap_response_take(response, &length));
    s_now_ms = 1000U;
    cmsis_dap_abort();
    cmsis_dap_process();
    assert(!cmsis_dap_response_take(response, &length));
    s_cancel_complete = true;
    cmsis_dap_process();
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

    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER;
    request[2] = 1U;
    request[3] = 0x22U;
    assert(cmsis_dap_submit(request, 4U));
    assert(response_take(response) == 3U);
    assert(response[2] == 0x08U);

    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER;
    request[2] = 1U;
    request[3] = 0x10U;
    assert(cmsis_dap_submit(request, 4U));
    assert(response_take(response) == 3U);
    assert(response[2] == 0x08U);

    /* 单轮完成回归：命令核心必须在取响应之前推进本地 SWD 引擎，使有线
     * block 不必多等一轮主循环调度。 */
    cmsis_dap_init();
    request[0] = DAP_CONNECT;
    request[1] = 1U;
    assert(cmsis_dap_submit(request, 2U));
    bridge_complete(SWD_TUNNEL_OP_CONNECT, 1U, TARGET_SWD_ACK_OK);
    assert(response_take(response) == 2U);
    assert(response[1] == 1U);

    memset(request, 0, sizeof(request));
    request[0] = DAP_TRANSFER;
    request[2] = 1U;
    request[3] = 0x00U;
    s_pump_calls = 0U;
    s_pump_completes = true;
    assert(cmsis_dap_submit(request, 8U));
    /* 一次 process 即可完成：pump 先执行，响应随后立即被取走。 */
    cmsis_dap_process();
    assert(s_pump_calls == 1U);
    assert(response_take(response) == 3U);
    assert(response[0] == DAP_TRANSFER);
    assert(response[1] == 1U);
    assert(response[2] == TARGET_SWD_ACK_OK);

    /* 官方 DAP_ExecuteCommands：多个完整子命令聚合为一个响应。 */
    memset(request, 0, sizeof(request));
    request[0] = DAP_EXECUTE_COMMANDS;
    request[1] = 2U;
    request[2] = DAP_INFO;
    request[3] = 0x01U;
    request[4] = DAP_INFO;
    request[5] = 0x02U;
    assert(cmsis_dap_submit(request, 6U));
    length = response_take(response);
    assert(length == 24U);
    assert(response[0] == DAP_EXECUTE_COMMANDS);
    assert(response[1] == 2U);
    assert(response[2] == DAP_INFO);
    assert(response[12] == DAP_INFO);

    return 0;
}
