#include "sx128x.h"

#include <string.h>

#define SX128X_COMMAND_TIMEOUT_MS    20U
#define SX128X_RF_FREQUENCY_HZ       2450000000UL
#define SX128X_RF_MIN_FREQUENCY_HZ   2400000000UL
#define SX128X_RF_MAX_FREQUENCY_HZ   2483500000UL
#define SX128X_XTAL_FREQUENCY_HZ     52000000UL
#define SX128X_FLRC_SYNC_WORD        0xD391DA26UL

#define SX128X_OPCODE_GET_STATUS             0xC0U
#define SX128X_OPCODE_WRITE_REGISTER         0x18U
#define SX128X_OPCODE_READ_BUFFER            0x1BU
#define SX128X_OPCODE_WRITE_BUFFER           0x1AU
#define SX128X_OPCODE_GET_RX_BUFFER_STATUS   0x17U
#define SX128X_OPCODE_SET_STANDBY            0x80U
#define SX128X_OPCODE_SET_RX                 0x82U
#define SX128X_OPCODE_SET_TX                 0x83U
#define SX128X_OPCODE_SET_RF_FREQUENCY       0x86U
#define SX128X_OPCODE_SET_PACKET_TYPE        0x8AU
#define SX128X_OPCODE_SET_MODULATION_PARAMS  0x8BU
#define SX128X_OPCODE_SET_PACKET_PARAMS      0x8CU
#define SX128X_OPCODE_SET_DIO_IRQ_PARAMS     0x8DU
#define SX128X_OPCODE_SET_TX_PARAMS          0x8EU
#define SX128X_OPCODE_SET_BUFFER_BASE        0x8FU
#define SX128X_OPCODE_GET_PACKET_TYPE        0x03U
#define SX128X_OPCODE_GET_PACKET_STATUS      0x1DU
#define SX128X_OPCODE_GET_IRQ_STATUS         0x15U
#define SX128X_OPCODE_CLEAR_IRQ_STATUS       0x97U

#define SX128X_PACKET_TYPE_GFSK       0x00U
#define SX128X_PACKET_TYPE_FLRC       0x03U
#define SX128X_STANDBY_RC             0x00U
#define SX128X_GFSK_BR_1_000_BW_1_2  0x45U
#define SX128X_GFSK_BR_2_000_BW_2_4  0x04U
#define SX128X_GFSK_BR_0_500_BW_0_6  0x86U
#define SX128X_GFSK_MOD_INDEX_0_5     0x01U
#define SX128X_GFSK_BT_0_5            0x20U
#define SX128X_GFSK_PREAMBLE_16_BITS  0x30U
#define SX128X_GFSK_SYNC_WORD_5_BYTES 0x08U
#define SX128X_GFSK_MATCH_SYNC_WORD_1 0x10U
#define SX128X_GFSK_CRC_2_BYTES       0x20U
#define SX128X_GFSK_WHITENING_ENABLE  0x00U
#define SX128X_FLRC_BR_1_300_BW_1_2  0x45U
#define SX128X_FLRC_BR_0_650_BW_0_6  0x86U
#define SX128X_FLRC_CR_3_4            0x02U
#define SX128X_FLRC_BT_0_5            0x20U
#define SX128X_FLRC_PREAMBLE_16_BITS  0x30U
#define SX128X_FLRC_SYNC_WORD_32_BITS 0x04U
#define SX128X_FLRC_MATCH_SYNC_WORD_1 0x10U
#define SX128X_PACKET_VARIABLE_LENGTH 0x20U
#define SX128X_FLRC_CRC_2_BYTES       0x10U
#define SX128X_FLRC_WHITENING_ENABLE  0x00U
#define SX128X_TX_POWER_MINUS_2_DBM   0x10U
#define SX128X_TX_RAMP_20_US          0xE0U
#define SX128X_TIMEOUT_BASE_1_MS      0x02U

#define SX128X_REG_FLRC_SYNC_WORD_1   0x09CFU
#define SX128X_REG_GFSK_SYNC_WORD_1   0x09CEU
#define SX128X_REG_GFSK_CRC_POLY      0x09C6U
#define SX128X_REG_GFSK_CRC_INIT      0x09C8U

typedef enum {
    SX128X_ACTIVE_PACKET_GFSK = 0,
    SX128X_ACTIVE_PACKET_FLRC
} sx128x_active_packet_t;

static sx128x_active_packet_t s_active_packet = SX128X_ACTIVE_PACKET_GFSK;
static sx128x_profile_t s_profile = SX128X_PROFILE_GFSK_1M;
static uint8_t s_network_sync_word[5] = {
    0xD3U, 0x91U, 0xDAU, 0x26U, 0xA5U
};
/*
 * The radio driver is single-instance and non-reentrant. Keeping the FIFO
 * transfer buffers here prevents a 260-byte temporary allocation on the
 * Cortex-M stack for every receive.
 */
static uint8_t s_fifo_tx_buffer[3U + SX128X_MAX_PAYLOAD_SIZE];
static uint8_t s_fifo_rx_buffer[3U + SX128X_MAX_PAYLOAD_SIZE];

static sx128x_result_t from_hal_result(radio_result_t result)
{
    return result == RADIO_RESULT_OK ? SX128X_RESULT_OK
                                     : SX128X_RESULT_HAL_ERROR;
}

static sx128x_result_t write_command(const uint8_t *command, size_t length)
{
    uint8_t response[10];

    if ((command == NULL) || (length == 0U) || (length > sizeof(response))) {
        return SX128X_RESULT_INVALID_ARGUMENT;
    }

    return from_hal_result(radio_hal_transaction(command, response, length,
                                                 SX128X_COMMAND_TIMEOUT_MS));
}

static sx128x_result_t write_register(uint16_t address,
                                      const uint8_t *data,
                                      size_t length)
{
    uint8_t frame[3U + 5U];

    if ((data == NULL) || (length == 0U) || (length > 5U)) {
        return SX128X_RESULT_INVALID_ARGUMENT;
    }

    frame[0] = SX128X_OPCODE_WRITE_REGISTER;
    frame[1] = (uint8_t)(address >> 8);
    frame[2] = (uint8_t)address;
    memcpy(&frame[3], data, length);
    return write_command(frame, 3U + length);
}

static sx128x_result_t set_rf_frequency(uint32_t frequency_hz)
{
    uint32_t pll_steps =
        (uint32_t)(((uint64_t)frequency_hz << 18) / SX128X_XTAL_FREQUENCY_HZ);
    uint8_t command[] = {
        SX128X_OPCODE_SET_RF_FREQUENCY,
        (uint8_t)(pll_steps >> 16),
        (uint8_t)(pll_steps >> 8),
        (uint8_t)pll_steps
    };

    return write_command(command, sizeof(command));
}

sx128x_result_t sx128x_set_frequency(uint32_t frequency_hz)
{
    if ((frequency_hz < SX128X_RF_MIN_FREQUENCY_HZ) ||
        (frequency_hz > SX128X_RF_MAX_FREQUENCY_HZ)) {
        return SX128X_RESULT_INVALID_ARGUMENT;
    }
    return set_rf_frequency(frequency_hz);
}

static sx128x_result_t set_packet_params(uint8_t payload_length)
{
    uint8_t command[8];

    command[0] = SX128X_OPCODE_SET_PACKET_PARAMS;
    command[4] = SX128X_PACKET_VARIABLE_LENGTH;
    command[5] = payload_length;

    if (s_active_packet == SX128X_ACTIVE_PACKET_GFSK) {
        command[1] = SX128X_GFSK_PREAMBLE_16_BITS;
        command[2] = SX128X_GFSK_SYNC_WORD_5_BYTES;
        command[3] = SX128X_GFSK_MATCH_SYNC_WORD_1;
        command[6] = SX128X_GFSK_CRC_2_BYTES;
        command[7] = SX128X_GFSK_WHITENING_ENABLE;
    } else {
        command[1] = SX128X_FLRC_PREAMBLE_16_BITS;
        command[2] = SX128X_FLRC_SYNC_WORD_32_BITS;
        command[3] = SX128X_FLRC_MATCH_SYNC_WORD_1;
        command[6] = SX128X_FLRC_CRC_2_BYTES;
        command[7] = SX128X_FLRC_WHITENING_ENABLE;
    }

    return write_command(command, sizeof(command));
}

sx128x_result_t sx128x_get_status(sx128x_status_t *status)
{
    uint8_t command = SX128X_OPCODE_GET_STATUS;
    uint8_t response;
    radio_result_t result;

    if (status == NULL) {
        return SX128X_RESULT_INVALID_ARGUMENT;
    }

    result = radio_hal_transaction(&command, &response, 1U,
                                   SX128X_COMMAND_TIMEOUT_MS);
    if (result != RADIO_RESULT_OK) {
        return SX128X_RESULT_HAL_ERROR;
    }

    status->raw = response;
    status->mode = (sx128x_mode_t)((response >> 5) & 0x07U);
    status->command_status = (response >> 2) & 0x07U;

    if ((status->command_status == 3U) ||
        (status->command_status == 4U) ||
        (status->command_status == 5U)) {
        return SX128X_RESULT_COMMAND_ERROR;
    }

    return SX128X_RESULT_OK;
}

sx128x_result_t sx128x_get_packet_status(sx128x_packet_status_t *status)
{
    uint8_t command[] = {
        SX128X_OPCODE_GET_PACKET_STATUS,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    uint8_t response[sizeof(command)];
    radio_result_t result;

    if (status == NULL) {
        return SX128X_RESULT_INVALID_ARGUMENT;
    }

    result = radio_hal_transaction(command, response, sizeof(command),
                                   SX128X_COMMAND_TIMEOUT_MS);
    if (result != RADIO_RESULT_OK) {
        return SX128X_RESULT_HAL_ERROR;
    }

    /* GFSK/FLRC packet status byte 1 contains -RSSI * 2. */
    status->rssi_dbm_x2 = -(int16_t)response[3];
    status->error_status = response[4];
    status->tx_rx_status = response[5];
    status->sync_address_status = response[6];
    return SX128X_RESULT_OK;
}

sx128x_result_t sx128x_standby(void)
{
    const uint8_t command[] = {
        SX128X_OPCODE_SET_STANDBY,
        SX128X_STANDBY_RC
    };

    radio_hal_frontend_set(RADIO_FRONTEND_STANDBY);
    return write_command(command, sizeof(command));
}

sx128x_result_t sx128x_init_flrc(void)
{
    sx128x_status_t status;
    uint8_t packet_type_command[] = {
        SX128X_OPCODE_SET_PACKET_TYPE,
        SX128X_PACKET_TYPE_FLRC
    };
    uint8_t buffer_command[] = {
        SX128X_OPCODE_SET_BUFFER_BASE,
        0x00U,
        0x80U
    };
    uint8_t modulation_command[] = {
        SX128X_OPCODE_SET_MODULATION_PARAMS,
        SX128X_FLRC_BR_1_300_BW_1_2,
        SX128X_FLRC_CR_3_4,
        SX128X_FLRC_BT_0_5
    };
    uint8_t tx_params_command[] = {
        SX128X_OPCODE_SET_TX_PARAMS,
        SX128X_TX_POWER_MINUS_2_DBM,
        SX128X_TX_RAMP_20_US
    };
    uint16_t irq_mask = SX128X_IRQ_TX_DONE | SX128X_IRQ_RX_DONE |
                        SX128X_IRQ_SYNC_WORD_ERROR | SX128X_IRQ_CRC_ERROR |
                        SX128X_IRQ_RX_TX_TIMEOUT;
    uint8_t irq_command[] = {
        SX128X_OPCODE_SET_DIO_IRQ_PARAMS,
        (uint8_t)(irq_mask >> 8),
        (uint8_t)irq_mask,
        (uint8_t)(irq_mask >> 8),
        (uint8_t)irq_mask,
        0x00U, 0x00U,
        0x00U, 0x00U
    };
    uint8_t sync_word[] = {
        (uint8_t)(SX128X_FLRC_SYNC_WORD >> 24),
        (uint8_t)(SX128X_FLRC_SYNC_WORD >> 16),
        (uint8_t)(SX128X_FLRC_SYNC_WORD >> 8),
        (uint8_t)SX128X_FLRC_SYNC_WORD
    };
    uint8_t get_type_command[] = {
        SX128X_OPCODE_GET_PACKET_TYPE,
        0x00U,
        0x00U
    };
    uint8_t get_type_response[sizeof(get_type_command)];
    sx128x_result_t result;

    s_active_packet = SX128X_ACTIVE_PACKET_FLRC;
    s_profile = SX128X_PROFILE_FLRC_1M3;
    result = sx128x_standby();
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = sx128x_get_status(&status);
    if ((result != SX128X_RESULT_OK) || (status.mode != SX128X_MODE_STDBY_RC)) {
        return SX128X_RESULT_VERIFY_ERROR;
    }
    result = write_command(packet_type_command, sizeof(packet_type_command));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = set_rf_frequency(SX128X_RF_FREQUENCY_HZ);
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = write_command(buffer_command, sizeof(buffer_command));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = write_command(modulation_command, sizeof(modulation_command));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = set_packet_params(SX128X_MAX_PAYLOAD_SIZE);
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = write_register(SX128X_REG_FLRC_SYNC_WORD_1, sync_word,
                            sizeof(sync_word));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = write_command(tx_params_command, sizeof(tx_params_command));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = write_command(irq_command, sizeof(irq_command));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = sx128x_clear_irq_status(SX128X_IRQ_ALL);
    if (result != SX128X_RESULT_OK) {
        return result;
    }

    result = from_hal_result(radio_hal_transaction(
        get_type_command, get_type_response, sizeof(get_type_command),
        SX128X_COMMAND_TIMEOUT_MS));
    if ((result != SX128X_RESULT_OK) ||
        (get_type_response[2] != SX128X_PACKET_TYPE_FLRC)) {
        return SX128X_RESULT_VERIFY_ERROR;
    }

    return SX128X_RESULT_OK;
}

sx128x_result_t sx128x_init_gfsk(void)
{
    static const uint8_t sync_word[5] = {
        0xD3U, 0x91U, 0xDAU, 0x26U, 0xA5U
    };
    static const uint8_t crc_polynomial[2] = {0x10U, 0x21U};
    static const uint8_t crc_initial[2] = {0xFFU, 0xFFU};
    sx128x_status_t status;
    uint8_t packet_type_command[] = {
        SX128X_OPCODE_SET_PACKET_TYPE,
        SX128X_PACKET_TYPE_GFSK
    };
    uint8_t buffer_command[] = {
        SX128X_OPCODE_SET_BUFFER_BASE,
        0x00U,
        0x80U
    };
    uint8_t modulation_command[] = {
        SX128X_OPCODE_SET_MODULATION_PARAMS,
        SX128X_GFSK_BR_1_000_BW_1_2,
        SX128X_GFSK_MOD_INDEX_0_5,
        SX128X_GFSK_BT_0_5
    };
    uint8_t tx_params_command[] = {
        SX128X_OPCODE_SET_TX_PARAMS,
        SX128X_TX_POWER_MINUS_2_DBM,
        SX128X_TX_RAMP_20_US
    };
    uint16_t irq_mask = SX128X_IRQ_TX_DONE | SX128X_IRQ_RX_DONE |
                        SX128X_IRQ_SYNC_WORD_ERROR | SX128X_IRQ_CRC_ERROR |
                        SX128X_IRQ_RX_TX_TIMEOUT;
    uint8_t irq_command[] = {
        SX128X_OPCODE_SET_DIO_IRQ_PARAMS,
        (uint8_t)(irq_mask >> 8),
        (uint8_t)irq_mask,
        (uint8_t)(irq_mask >> 8),
        (uint8_t)irq_mask,
        0x00U, 0x00U,
        0x00U, 0x00U
    };
    uint8_t get_type_command[] = {
        SX128X_OPCODE_GET_PACKET_TYPE,
        0x00U,
        0x00U
    };
    uint8_t get_type_response[sizeof(get_type_command)];
    sx128x_result_t result;

    s_active_packet = SX128X_ACTIVE_PACKET_GFSK;
    s_profile = SX128X_PROFILE_GFSK_1M;
    result = sx128x_standby();
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = sx128x_get_status(&status);
    if ((result != SX128X_RESULT_OK) || (status.mode != SX128X_MODE_STDBY_RC)) {
        return SX128X_RESULT_VERIFY_ERROR;
    }
    result = write_command(packet_type_command, sizeof(packet_type_command));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = set_rf_frequency(SX128X_RF_FREQUENCY_HZ);
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = write_command(buffer_command, sizeof(buffer_command));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = write_command(modulation_command, sizeof(modulation_command));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = set_packet_params(SX128X_MAX_PAYLOAD_SIZE);
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = write_register(SX128X_REG_GFSK_SYNC_WORD_1, sync_word,
                            sizeof(sync_word));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = write_register(SX128X_REG_GFSK_CRC_POLY, crc_polynomial,
                            sizeof(crc_polynomial));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = write_register(SX128X_REG_GFSK_CRC_INIT, crc_initial,
                            sizeof(crc_initial));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = write_command(tx_params_command, sizeof(tx_params_command));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = write_command(irq_command, sizeof(irq_command));
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = sx128x_clear_irq_status(SX128X_IRQ_ALL);
    if (result != SX128X_RESULT_OK) {
        return result;
    }

    result = from_hal_result(radio_hal_transaction(
        get_type_command, get_type_response, sizeof(get_type_command),
        SX128X_COMMAND_TIMEOUT_MS));
    if ((result != SX128X_RESULT_OK) ||
        (get_type_response[2] != SX128X_PACKET_TYPE_GFSK)) {
        return SX128X_RESULT_VERIFY_ERROR;
    }

    return SX128X_RESULT_OK;
}

sx128x_result_t sx128x_set_profile(sx128x_profile_t profile)
{
    uint8_t packet_type;
    uint8_t modulation_0;
    uint8_t modulation_1;
    uint8_t modulation_2;
    uint8_t packet_type_command[2];
    uint8_t modulation_command[4];
    sx128x_result_t result;

    if (profile >= SX128X_PROFILE_COUNT) {
        return SX128X_RESULT_INVALID_ARGUMENT;
    }

    switch (profile) {
    case SX128X_PROFILE_GFSK_2M:
        packet_type = SX128X_PACKET_TYPE_GFSK;
        modulation_0 = SX128X_GFSK_BR_2_000_BW_2_4;
        modulation_1 = SX128X_GFSK_MOD_INDEX_0_5;
        modulation_2 = SX128X_GFSK_BT_0_5;
        break;
    case SX128X_PROFILE_GFSK_1M:
        packet_type = SX128X_PACKET_TYPE_GFSK;
        modulation_0 = SX128X_GFSK_BR_1_000_BW_1_2;
        modulation_1 = SX128X_GFSK_MOD_INDEX_0_5;
        modulation_2 = SX128X_GFSK_BT_0_5;
        break;
    case SX128X_PROFILE_GFSK_500K:
        packet_type = SX128X_PACKET_TYPE_GFSK;
        modulation_0 = SX128X_GFSK_BR_0_500_BW_0_6;
        modulation_1 = SX128X_GFSK_MOD_INDEX_0_5;
        modulation_2 = SX128X_GFSK_BT_0_5;
        break;
    case SX128X_PROFILE_FLRC_1M3:
        packet_type = SX128X_PACKET_TYPE_FLRC;
        modulation_0 = SX128X_FLRC_BR_1_300_BW_1_2;
        modulation_1 = SX128X_FLRC_CR_3_4;
        modulation_2 = SX128X_FLRC_BT_0_5;
        break;
    case SX128X_PROFILE_FLRC_650K:
        packet_type = SX128X_PACKET_TYPE_FLRC;
        modulation_0 = SX128X_FLRC_BR_0_650_BW_0_6;
        modulation_1 = SX128X_FLRC_CR_3_4;
        modulation_2 = SX128X_FLRC_BT_0_5;
        break;
    default:
        return SX128X_RESULT_INVALID_ARGUMENT;
    }

    result = sx128x_standby();
    if (result != SX128X_RESULT_OK) {
        return result;
    }

    packet_type_command[0] = SX128X_OPCODE_SET_PACKET_TYPE;
    packet_type_command[1] = packet_type;
    result = write_command(packet_type_command, sizeof(packet_type_command));
    if (result != SX128X_RESULT_OK) {
        return result;
    }

    modulation_command[0] = SX128X_OPCODE_SET_MODULATION_PARAMS;
    modulation_command[1] = modulation_0;
    modulation_command[2] = modulation_1;
    modulation_command[3] = modulation_2;
    result = write_command(modulation_command, sizeof(modulation_command));
    if (result != SX128X_RESULT_OK) {
        return result;
    }

    s_active_packet = packet_type == SX128X_PACKET_TYPE_GFSK
                          ? SX128X_ACTIVE_PACKET_GFSK
                          : SX128X_ACTIVE_PACKET_FLRC;
    result = set_packet_params(SX128X_MAX_PAYLOAD_SIZE);
    if (result != SX128X_RESULT_OK) {
        return result;
    }

    if (s_active_packet == SX128X_ACTIVE_PACKET_GFSK) {
        result = write_register(SX128X_REG_GFSK_SYNC_WORD_1,
                                s_network_sync_word,
                                sizeof(s_network_sync_word));
    } else {
        result = write_register(SX128X_REG_FLRC_SYNC_WORD_1,
                                s_network_sync_word,
                                sizeof(s_network_sync_word) - 1U);
    }
    if (result != SX128X_RESULT_OK) {
        return result;
    }

    s_profile = profile;
    return sx128x_clear_irq_status(SX128X_IRQ_ALL);
}

sx128x_result_t sx128x_set_network_sync(const uint8_t sync_word[5])
{
    if (sync_word == NULL) {
        return SX128X_RESULT_INVALID_ARGUMENT;
    }

    memcpy(s_network_sync_word, sync_word, sizeof(s_network_sync_word));
    if (s_active_packet == SX128X_ACTIVE_PACKET_GFSK) {
        return write_register(SX128X_REG_GFSK_SYNC_WORD_1,
                              s_network_sync_word,
                              sizeof(s_network_sync_word));
    }
    return write_register(SX128X_REG_FLRC_SYNC_WORD_1,
                          s_network_sync_word,
                          sizeof(s_network_sync_word) - 1U);
}

sx128x_profile_t sx128x_get_profile(void)
{
    return s_profile;
}

const char *sx128x_profile_name(sx128x_profile_t profile)
{
    static const char *const names[SX128X_PROFILE_COUNT] = {
        "GFSK2M",
        "GFSK1M",
        "GFSK500K",
        "FLRC1M3",
        "FLRC650K"
    };

    return profile < SX128X_PROFILE_COUNT ? names[profile] : "INVALID";
}

sx128x_result_t sx128x_get_irq_status(uint16_t *irq_status)
{
    uint8_t command[] = {
        SX128X_OPCODE_GET_IRQ_STATUS,
        0x00U, 0x00U, 0x00U
    };
    uint8_t response[sizeof(command)];
    radio_result_t result;

    if (irq_status == NULL) {
        return SX128X_RESULT_INVALID_ARGUMENT;
    }

    result = radio_hal_transaction(command, response, sizeof(command),
                                   SX128X_COMMAND_TIMEOUT_MS);
    if (result != RADIO_RESULT_OK) {
        return SX128X_RESULT_HAL_ERROR;
    }

    *irq_status = ((uint16_t)response[2] << 8) | response[3];
    return SX128X_RESULT_OK;
}

sx128x_result_t sx128x_clear_irq_status(uint16_t irq_mask)
{
    uint8_t command[] = {
        SX128X_OPCODE_CLEAR_IRQ_STATUS,
        (uint8_t)(irq_mask >> 8),
        (uint8_t)irq_mask
    };

    return write_command(command, sizeof(command));
}

sx128x_result_t sx128x_write_buffer(uint8_t offset,
                                    const uint8_t *data,
                                    size_t length)
{
    if ((data == NULL) || (length == 0U) ||
        (length > SX128X_MAX_PAYLOAD_SIZE)) {
        return SX128X_RESULT_INVALID_ARGUMENT;
    }

    s_fifo_tx_buffer[0] = SX128X_OPCODE_WRITE_BUFFER;
    s_fifo_tx_buffer[1] = offset;
    memcpy(&s_fifo_tx_buffer[2], data, length);
    return from_hal_result(radio_hal_transaction(
        s_fifo_tx_buffer, NULL, length + 2U, SX128X_COMMAND_TIMEOUT_MS));
}

sx128x_result_t sx128x_read_buffer(uint8_t offset,
                                   uint8_t *data,
                                   size_t length)
{
    radio_result_t result;

    if ((data == NULL) || (length == 0U) ||
        (length > SX128X_MAX_PAYLOAD_SIZE)) {
        return SX128X_RESULT_INVALID_ARGUMENT;
    }

    memset(s_fifo_tx_buffer, 0, length + 3U);
    s_fifo_tx_buffer[0] = SX128X_OPCODE_READ_BUFFER;
    s_fifo_tx_buffer[1] = offset;
    result = radio_hal_transaction(s_fifo_tx_buffer, s_fifo_rx_buffer,
                                   length + 3U,
                                   SX128X_COMMAND_TIMEOUT_MS);
    if (result != RADIO_RESULT_OK) {
        return SX128X_RESULT_HAL_ERROR;
    }

    memcpy(data, &s_fifo_rx_buffer[3], length);
    return SX128X_RESULT_OK;
}

sx128x_result_t sx128x_get_rx_buffer_status(uint8_t *payload_length,
                                            uint8_t *buffer_offset)
{
    uint8_t command[] = {
        SX128X_OPCODE_GET_RX_BUFFER_STATUS,
        0x00U, 0x00U, 0x00U
    };
    uint8_t response[sizeof(command)];
    radio_result_t result;

    if ((payload_length == NULL) || (buffer_offset == NULL)) {
        return SX128X_RESULT_INVALID_ARGUMENT;
    }

    result = radio_hal_transaction(command, response, sizeof(command),
                                   SX128X_COMMAND_TIMEOUT_MS);
    if (result != RADIO_RESULT_OK) {
        return SX128X_RESULT_HAL_ERROR;
    }

    *payload_length = response[2];
    *buffer_offset = response[3];
    return SX128X_RESULT_OK;
}

sx128x_result_t sx128x_start_tx(const uint8_t *data, size_t length)
{
    uint8_t command[] = {
        SX128X_OPCODE_SET_TX,
        SX128X_TIMEOUT_BASE_1_MS,
        0x00U,
        100U
    };
    sx128x_result_t result;

    if ((data == NULL) || (length < 6U) ||
        (length > SX128X_MAX_PAYLOAD_SIZE)) {
        return SX128X_RESULT_INVALID_ARGUMENT;
    }

    result = set_packet_params((uint8_t)length);
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = sx128x_write_buffer(0x00U, data, length);
    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = sx128x_clear_irq_status(SX128X_IRQ_ALL);
    if (result != SX128X_RESULT_OK) {
        return result;
    }

    radio_hal_frontend_set(RADIO_FRONTEND_TRANSMIT);
    result = write_command(command, sizeof(command));
    if (result != SX128X_RESULT_OK) {
        radio_hal_frontend_set(RADIO_FRONTEND_STANDBY);
    }
    return result;
}

sx128x_result_t sx128x_start_rx(uint16_t timeout_ms)
{
    uint16_t count = timeout_ms == 0U ? 0xFFFFU : timeout_ms;
    uint8_t command[] = {
        SX128X_OPCODE_SET_RX,
        SX128X_TIMEOUT_BASE_1_MS,
        (uint8_t)(count >> 8),
        (uint8_t)count
    };
    sx128x_result_t result = set_packet_params(SX128X_MAX_PAYLOAD_SIZE);

    if (result != SX128X_RESULT_OK) {
        return result;
    }
    result = sx128x_clear_irq_status(SX128X_IRQ_ALL);
    if (result != SX128X_RESULT_OK) {
        return result;
    }

    radio_hal_frontend_set(RADIO_FRONTEND_RECEIVE);
    result = write_command(command, sizeof(command));
    if (result != SX128X_RESULT_OK) {
        radio_hal_frontend_set(RADIO_FRONTEND_STANDBY);
    }
    return result;
}
