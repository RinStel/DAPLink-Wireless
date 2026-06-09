#include "radio_hal.h"

#include "board.h"
#include "board_pins.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_spi.h"

#define RADIO_SPI                    SPI0
#define RADIO_SPI_TIMEOUT_MS         10U
#define RADIO_RESET_HOLD_MS          1U
#define RADIO_RESET_READY_TIMEOUT_MS 20U

static bool timeout_expired(uint32_t start, uint32_t timeout_ms)
{
    return (uint32_t)(board_millis() - start) >= timeout_ms;
}

static radio_result_t spi_transfer_byte(uint8_t tx_data,
                                        uint8_t *rx_data,
                                        uint32_t timeout_ms)
{
    uint32_t start = board_millis();

    while (spi_i2s_flag_get(RADIO_SPI, SPI_FLAG_TBE) == RESET) {
        if (timeout_expired(start, timeout_ms)) {
            return RADIO_RESULT_SPI_TIMEOUT;
        }
    }

    spi_i2s_data_transmit(RADIO_SPI, tx_data);

    while (spi_i2s_flag_get(RADIO_SPI, SPI_FLAG_RBNE) == RESET) {
        if (timeout_expired(start, timeout_ms)) {
            return RADIO_RESULT_SPI_TIMEOUT;
        }
    }

    *rx_data = (uint8_t)spi_i2s_data_receive(RADIO_SPI);
    return RADIO_RESULT_OK;
}

static void spi_init_for_radio(void)
{
    spi_parameter_struct config;

    rcu_periph_clock_enable(RCU_SPI0);

    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ,
              GPIO_PIN_5 | GPIO_PIN_7);
    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_6);

    spi_i2s_deinit(RADIO_SPI);
    spi_struct_para_init(&config);
    config.device_mode = SPI_MASTER;
    config.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    config.frame_size = SPI_FRAMESIZE_8BIT;
    config.nss = SPI_NSS_SOFT;
    config.endian = SPI_ENDIAN_MSB;
    config.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;

    /* APB2 is 120 MHz. /16 gives 7.5 MHz, below the module's 10 MHz limit. */
    config.prescale = SPI_PSC_16;
    spi_init(RADIO_SPI, &config);
    spi_enable(RADIO_SPI);
}

radio_result_t radio_hal_init(void)
{
    radio_hal_frontend_set(RADIO_FRONTEND_STANDBY);
    gpio_bit_set(BOARD_RF_NSS_PORT, BOARD_RF_NSS_PIN);
    spi_init_for_radio();
    return radio_hal_reset(RADIO_RESET_READY_TIMEOUT_MS);
}

radio_result_t radio_hal_reset(uint32_t timeout_ms)
{
    radio_hal_frontend_set(RADIO_FRONTEND_STANDBY);
    gpio_bit_reset(BOARD_RF_NRESET_PORT, BOARD_RF_NRESET_PIN);
    board_delay_ms(RADIO_RESET_HOLD_MS);
    gpio_bit_set(BOARD_RF_NRESET_PORT, BOARD_RF_NRESET_PIN);
    return radio_hal_wait_ready(timeout_ms);
}

radio_result_t radio_hal_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = board_millis();

    while (gpio_input_bit_get(BOARD_RF_BUSY_PORT, BOARD_RF_BUSY_PIN) != RESET) {
        if (timeout_expired(start, timeout_ms)) {
            return RADIO_RESULT_BUSY_TIMEOUT;
        }
    }

    return RADIO_RESULT_OK;
}

radio_result_t radio_hal_transaction(const uint8_t *tx_data,
                                     uint8_t *rx_data,
                                     size_t length,
                                     uint32_t timeout_ms)
{
    size_t index;
    uint32_t transfer_start;
    uint8_t ignored_rx;
    radio_result_t result;

    if ((tx_data == NULL) || (length == 0U)) {
        return RADIO_RESULT_INVALID_ARGUMENT;
    }

    result = radio_hal_wait_ready(timeout_ms);
    if (result != RADIO_RESULT_OK) {
        return result;
    }

    gpio_bit_reset(BOARD_RF_NSS_PORT, BOARD_RF_NSS_PIN);

    for (index = 0U; index < length; ++index) {
        uint8_t *destination = (rx_data != NULL) ? &rx_data[index] : &ignored_rx;

        result = spi_transfer_byte(tx_data[index], destination,
                                   RADIO_SPI_TIMEOUT_MS);
        if (result != RADIO_RESULT_OK) {
            break;
        }
    }

    transfer_start = board_millis();
    while ((result == RADIO_RESULT_OK) &&
           (spi_i2s_flag_get(RADIO_SPI, SPI_FLAG_TRANS) != RESET)) {
        if (timeout_expired(transfer_start, RADIO_SPI_TIMEOUT_MS)) {
            result = RADIO_RESULT_SPI_TIMEOUT;
        }
    }

    gpio_bit_set(BOARD_RF_NSS_PORT, BOARD_RF_NSS_PIN);
    if (result != RADIO_RESULT_OK) {
        return result;
    }

    return radio_hal_wait_ready(timeout_ms);
}

void radio_hal_frontend_set(radio_frontend_mode_t mode)
{
    bool rx_enable = mode == RADIO_FRONTEND_RECEIVE;
    bool tx_enable = mode == RADIO_FRONTEND_TRANSMIT;

    if (rx_enable) {
        gpio_bit_set(BOARD_RF_RX_EN_PORT, BOARD_RF_RX_EN_PIN);
    } else {
        gpio_bit_reset(BOARD_RF_RX_EN_PORT, BOARD_RF_RX_EN_PIN);
    }

    if (tx_enable) {
        gpio_bit_set(BOARD_RF_TX_EN_PORT, BOARD_RF_TX_EN_PIN);
    } else {
        gpio_bit_reset(BOARD_RF_TX_EN_PORT, BOARD_RF_TX_EN_PIN);
    }
}

bool radio_hal_irq_active(void)
{
    return gpio_input_bit_get(BOARD_RF_DIO1_PORT, BOARD_RF_DIO1_PIN) != RESET;
}
