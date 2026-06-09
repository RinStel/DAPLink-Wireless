/*
 * DAPLink-Wireless — Wireless CMSIS-DAP v2 debug probe firmware
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "serial_service.h"

#include <stddef.h>

#include "cdc_acm_transport.h"
#include "target_uart.h"

static uint32_t decode_u32_be(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24) |
           ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) |
           input[3];
}

static void encode_u32_be(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static bool line_to_uart(const acm_line *line, target_uart_config_t *uart)
{
    if ((line->dwDTERate < 1200U) ||
        (line->dwDTERate > 3000000U) ||
        ((line->bDataBits != 7U) && (line->bDataBits != 8U)) ||
        (line->bCharFormat == 1U) ||
        (line->bCharFormat > 2U) ||
        ((line->bDataBits == 7U) && (line->bParityType == 0U))) {
        return false;
    }
    uart->baud_rate = line->dwDTERate;
    uart->data_bits = line->bDataBits;
    uart->stop_bits = line->bCharFormat == 2U ? 2U : 1U;
    if (line->bParityType == 0U) {
        uart->parity = TARGET_UART_PARITY_NONE;
    } else if (line->bParityType == 1U) {
        uart->parity = TARGET_UART_PARITY_ODD;
    } else if (line->bParityType == 2U) {
        uart->parity = TARGET_UART_PARITY_EVEN;
    } else {
        return false;
    }
    return true;
}

bool serial_service_init(void)
{
    const target_uart_config_t uart = {
        .baud_rate = 115200U,
        .data_bits = 8U,
        .stop_bits = 1U,
        .parity = TARGET_UART_PARITY_NONE
    };

    return target_uart_init(&uart);
}

void serial_service_process(void)
{
    target_uart_process();
}

bool serial_service_wired_process(void)
{
    uint8_t data[RADIO_PROTOCOL_PAYLOAD_SIZE];
    uint16_t usb_length;
    size_t uart_length;
    acm_line line;
    target_uart_config_t uart;
    bool activity = false;

    if (cdc_acm_line_coding_take(&line) &&
        line_to_uart(&line, &uart)) {
        (void)target_uart_configure(&uart);
    }
    usb_length = target_uart_tx_free() >= sizeof(data)
                     ? cdc_acm_read(data, sizeof(data))
                     : 0U;
    if (usb_length != 0U) {
        (void)target_uart_write(data, usb_length);
        activity = true;
    }
    uart_length = cdc_acm_tx_ready()
                      ? target_uart_read(data, sizeof(data))
                      : 0U;
    if (uart_length != 0U) {
        (void)cdc_acm_write(data, (uint16_t)uart_length);
        activity = true;
    }
    return activity;
}

bool serial_service_deliver_data(device_mode_t mode,
                                 const uint8_t *payload, uint8_t length)
{
    if ((payload == NULL) || (length == 0U)) {
        return false;
    }
    if (mode == DEVICE_MODE_WIRELESS_HOST) {
        return cdc_acm_write(payload, length) == length;
    }
    if (mode == DEVICE_MODE_WIRELESS_SLAVE) {
        return target_uart_write(payload, length) == length;
    }
    return false;
}

bool serial_service_deliver_line_coding(const uint8_t *payload,
                                        uint8_t length)
{
    acm_line line;
    target_uart_config_t uart;

    if ((payload == NULL) || (length != 7U)) {
        return false;
    }
    line.dwDTERate = decode_u32_be(payload);
    line.bCharFormat = payload[4];
    line.bParityType = payload[5];
    line.bDataBits = payload[6];
    return line_to_uart(&line, &uart) && target_uart_configure(&uart);
}

bool serial_service_source_take(device_mode_t mode,
                                radio_frame_type_t *type,
                                uint8_t *payload, uint8_t *length)
{
    if ((type == NULL) || (payload == NULL) || (length == NULL)) {
        return false;
    }
    if (mode == DEVICE_MODE_WIRELESS_HOST) {
        acm_line line;
        target_uart_config_t ignored_uart;
        uint16_t data_length;

        if (cdc_acm_line_coding_take(&line) &&
            line_to_uart(&line, &ignored_uart)) {
            encode_u32_be(payload, line.dwDTERate);
            payload[4] = line.bCharFormat;
            payload[5] = line.bParityType;
            payload[6] = line.bDataBits;
            *type = RADIO_FRAME_LINE_CODING;
            *length = 7U;
            return true;
        }
        data_length =
            cdc_acm_read(payload, RADIO_PROTOCOL_PAYLOAD_SIZE);
        if (data_length != 0U) {
            *type = RADIO_FRAME_DATA;
            *length = (uint8_t)data_length;
            return true;
        }
    } else if (mode == DEVICE_MODE_WIRELESS_SLAVE) {
        size_t data_length =
            target_uart_read(payload, RADIO_PROTOCOL_PAYLOAD_SIZE);

        if (data_length != 0U) {
            *type = RADIO_FRAME_DATA;
            *length = (uint8_t)data_length;
            return true;
        }
    }
    return false;
}

uint32_t serial_service_rx_overruns(void)
{
    return target_uart_rx_overruns();
}
