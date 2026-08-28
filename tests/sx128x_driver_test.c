#include <assert.h>
#include <string.h>

#include "radio_hal.h"
#include "sx128x.h"

/* SPI 替身返回芯片状态/数据，逐字节校验命令帧。 */
#define OPCODE_SET_PACKET_TYPE    0x8AU
#define OPCODE_SET_PACKET_PARAMS  0x8CU
#define OPCODE_SET_STANDBY        0x80U
#define OPCODE_SET_RX             0x82U
#define OPCODE_SET_TX             0x83U
#define OPCODE_GET_PACKET_TYPE    0x03U
#define OPCODE_SET_DIO_IRQ_PARAMS 0x8DU
#define OPCODE_GET_STATUS         0xC0U

typedef enum {
    TEST_RADIO_STANDBY = 0,
    TEST_RADIO_RECEIVE,
    TEST_RADIO_TRANSMIT
} test_radio_mode_t;

static uint8_t s_packet_type;
static uint16_t s_irq_mask;
static uint16_t s_dio1_mask;
static uint8_t s_get_status_length;
static uint16_t s_packet_params_commands;
static test_radio_mode_t s_radio_mode;
static bool s_packet_params_while_active;

radio_result_t radio_hal_transaction(const uint8_t *tx_data,
                                     uint8_t *rx_data,
                                     size_t length,
                                     uint32_t timeout_ms)
{
    (void)timeout_ms;
    assert(tx_data != NULL);
    assert(length != 0U);
    if (rx_data != NULL) {
        memset(rx_data, 0, length);
    }
    if ((tx_data[0] == OPCODE_SET_PACKET_TYPE) && (length == 2U)) {
        s_packet_type = tx_data[1];
    } else if ((tx_data[0] == OPCODE_SET_STANDBY) && (length == 2U)) {
        s_radio_mode = TEST_RADIO_STANDBY;
    } else if ((tx_data[0] == OPCODE_SET_RX) && (length == 4U)) {
        s_radio_mode = TEST_RADIO_RECEIVE;
    } else if ((tx_data[0] == OPCODE_SET_TX) && (length == 4U)) {
        s_radio_mode = TEST_RADIO_TRANSMIT;
    } else if ((tx_data[0] == OPCODE_GET_PACKET_TYPE) &&
               (length == 3U)) {
        assert(rx_data != NULL);
        rx_data[2] = s_packet_type;
    } else if ((tx_data[0] == OPCODE_SET_DIO_IRQ_PARAMS) &&
               (length == 9U)) {
        s_irq_mask = ((uint16_t)tx_data[1] << 8) | tx_data[2];
        s_dio1_mask = ((uint16_t)tx_data[3] << 8) | tx_data[4];
    } else if ((tx_data[0] == OPCODE_SET_PACKET_PARAMS) &&
               (length == 8U)) {
        if (s_radio_mode != TEST_RADIO_STANDBY) {
            s_packet_params_while_active = true;
        }
        ++s_packet_params_commands;
    } else if ((tx_data[0] == OPCODE_GET_STATUS) && (length == 2U)) {
        assert(rx_data != NULL);
        s_get_status_length = (uint8_t)length;
        rx_data[1] = (uint8_t)(SX128X_MODE_STDBY_RC << 5);
    }
    return RADIO_RESULT_OK;
}

void radio_hal_frontend_set(radio_frontend_mode_t mode)
{
    (void)mode;
}

static void test_tx_enters_standby_before_packet_params(void)
{
    uint8_t payload[17U] = {0U};

    s_packet_params_while_active = false;
    assert(sx128x_init_gfsk() == SX128X_RESULT_OK);
    assert(sx128x_start_rx(0U) == SX128X_RESULT_OK);
    assert(sx128x_start_tx(payload, sizeof(payload)) == SX128X_RESULT_OK);
    assert(!s_packet_params_while_active);
}

static void test_packet_params_cache_tracks_current_hardware_state(void)
{
    uint8_t payload[17U] = {0U};
    uint16_t commands;

    assert(sx128x_init_gfsk() == SX128X_RESULT_OK);
    commands = s_packet_params_commands;
    assert(sx128x_start_tx(payload, sizeof(payload)) == SX128X_RESULT_OK);
    assert(s_packet_params_commands == (uint16_t)(commands + 1U));

    /* TX_DONE 后 SX1281 自动返回 standby。 */
    s_radio_mode = TEST_RADIO_STANDBY;
    commands = s_packet_params_commands;
    assert(sx128x_start_rx(0U) == SX128X_RESULT_OK);
    assert(s_packet_params_commands == (uint16_t)(commands + 1U));

    assert(sx128x_standby() == SX128X_RESULT_OK);
    commands = s_packet_params_commands;
    assert(sx128x_start_tx(payload, sizeof(payload)) == SX128X_RESULT_OK);
    assert(s_packet_params_commands == (uint16_t)(commands + 1U));
}

int main(void)
{
    const uint16_t required = SX128X_IRQ_TX_DONE | SX128X_IRQ_RX_DONE |
                              SX128X_IRQ_SYNC_WORD_ERROR |
                              SX128X_IRQ_CRC_ERROR |
                              SX128X_IRQ_RX_TX_TIMEOUT;

    assert(sx128x_init_gfsk() == SX128X_RESULT_OK);
    assert(s_get_status_length == 2U);
    assert(s_packet_type == 0x00U);
    assert(s_irq_mask == required);
    assert(s_dio1_mask == required);
    assert((s_dio1_mask & SX128X_IRQ_SYNC_WORD_VALID) == 0U);

    assert(sx128x_init_flrc() == SX128X_RESULT_OK);
    assert(s_packet_type == 0x03U);
    assert(s_irq_mask == required);
    assert(s_dio1_mask == required);
    test_tx_enters_standby_before_packet_params();
    test_packet_params_cache_tracks_current_hardware_state();
    return 0;
}
