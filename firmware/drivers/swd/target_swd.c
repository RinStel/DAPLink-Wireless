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
#include "target_swd.h"

#include <stddef.h>
#include <string.h>

#include "board.h"
#include "board_pins.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"

#define TARGET_SWD_DEFAULT_CLOCK_HZ  100000U
#define TARGET_SWD_MIN_CLOCK_HZ      10000U
#define TARGET_SWD_MAX_CLOCK_HZ      1000000U
#define TARGET_SWD_DEFAULT_RETRIES   100U
#define TARGET_SWD_MAX_WAIT_RETRIES  1024U
#define TARGET_SWD_TRANSFER_BUDGET_MS 1000U

static uint32_t s_half_period_cycles;
static uint32_t s_next_edge_cycle;
static uint16_t s_wait_retries = TARGET_SWD_DEFAULT_RETRIES;
static uint8_t s_idle_cycles = 8U;
static uint8_t s_turnaround = 1U;
static bool s_data_phase;
static volatile bool s_abort_requested;

static void clock_delay(void)
{
    uint32_t now = board_cycle_count();

    if ((int32_t)(now - s_next_edge_cycle) >=
        (int32_t)s_half_period_cycles) {
        s_next_edge_cycle = now;
    }
    while ((int32_t)(board_cycle_count() - s_next_edge_cycle) < 0) {
    }
    s_next_edge_cycle += s_half_period_cycles;
}

static void swclk_write(bool high)
{
    if (high) {
        GPIO_BOP(BOARD_TGT_SWCLK_PORT) = BOARD_TGT_SWCLK_PIN;
    } else {
        GPIO_BC(BOARD_TGT_SWCLK_PORT) = BOARD_TGT_SWCLK_PIN;
    }
}

static void swdio_write(bool high)
{
    if (high) {
        GPIO_BOP(BOARD_TGT_SWDIO_PORT) = BOARD_TGT_SWDIO_PIN;
    } else {
        GPIO_BC(BOARD_TGT_SWDIO_PORT) = BOARD_TGT_SWDIO_PIN;
    }
}

static void swdio_output(void)
{
    gpio_init(BOARD_TGT_SWDIO_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              BOARD_TGT_SWDIO_PIN);
}

static void swdio_input(void)
{
    gpio_init(BOARD_TGT_SWDIO_PORT, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ,
              BOARD_TGT_SWDIO_PIN);
}

static void clock_cycle(void)
{
    clock_delay();
    swclk_write(true);
    clock_delay();
    swclk_write(false);
}

static void write_bit(uint32_t bit)
{
    swdio_write(bit != 0U);
    clock_cycle();
}

static uint32_t read_bit(void)
{
    uint32_t bit;

    clock_delay();
    swclk_write(true);
    clock_delay();
    bit = GPIO_ISTAT(BOARD_TGT_SWDIO_PORT) & BOARD_TGT_SWDIO_PIN;
    swclk_write(false);
    return bit != 0U ? 1U : 0U;
}

static void write_bits(uint32_t value, uint8_t count)
{
    uint8_t index;

    for (index = 0U; index < count; ++index) {
        write_bit(value & 1U);
        value >>= 1;
    }
}

static uint32_t read_bits(uint8_t count)
{
    uint32_t value = 0U;
    uint8_t index;

    for (index = 0U; index < count; ++index) {
        value |= read_bit() << index;
    }
    return value;
}

static uint32_t parity32(uint32_t value)
{
    value ^= value >> 16;
    value ^= value >> 8;
    value ^= value >> 4;
    value &= 0xFU;
    return (0x6996U >> value) & 1U;
}

static uint8_t request_packet(uint8_t request)
{
    uint8_t fields = request & 0x0FU;
    uint8_t parity = (uint8_t)parity32(fields);

    return (uint8_t)(0x81U | (fields << 1) | (parity << 5));
}

static target_swd_ack_t transfer_once(uint8_t request, uint32_t *data)
{
    target_swd_ack_t ack;
    bool read = (request & 0x02U) != 0U;
    uint8_t index;

    swdio_output();
    write_bits(request_packet(request), 8U);
    swdio_input();
    for (index = 0U; index < s_turnaround; ++index) {
        clock_cycle();
    }
    ack = (target_swd_ack_t)read_bits(3U);

    if (ack == TARGET_SWD_ACK_OK) {
        if (read) {
            uint32_t value = read_bits(32U);
            uint32_t parity = read_bit();

            for (index = 0U; index < s_turnaround; ++index) {
                clock_cycle();
            }
            swdio_output();
            if (parity != parity32(value)) {
                ack = TARGET_SWD_ACK_PARITY;
            } else if (data != NULL) {
                *data = value;
            }
        } else {
            uint32_t value = data != NULL ? *data : 0U;

            for (index = 0U; index < s_turnaround; ++index) {
                clock_cycle();
            }
            swdio_output();
            write_bits(value, 32U);
            write_bit(parity32(value));
        }
    } else {
        if (s_data_phase) {
            if (read) {
                (void)read_bits(32U);
                (void)read_bit();
                for (index = 0U; index < s_turnaround; ++index) {
                    clock_cycle();
                }
                swdio_output();
            } else {
                for (index = 0U; index < s_turnaround; ++index) {
                    clock_cycle();
                }
                swdio_output();
                write_bits(0U, 32U);
                write_bit(0U);
            }
        } else {
            for (index = 0U; index < s_turnaround; ++index) {
                clock_cycle();
            }
            swdio_output();
        }
        if ((ack != TARGET_SWD_ACK_WAIT) &&
            (ack != TARGET_SWD_ACK_FAULT)) {
            ack = TARGET_SWD_ACK_PROTOCOL;
        }
    }

    swdio_write(false);
    write_bits(0U, s_idle_cycles);
    swdio_input();
    return ack;
}

void target_swd_init(uint32_t clock_hz)
{
    if (clock_hz == 0U) {
        clock_hz = TARGET_SWD_DEFAULT_CLOCK_HZ;
    } else if (clock_hz < TARGET_SWD_MIN_CLOCK_HZ) {
        clock_hz = TARGET_SWD_MIN_CLOCK_HZ;
    } else if (clock_hz > TARGET_SWD_MAX_CLOCK_HZ) {
        clock_hz = TARGET_SWD_MAX_CLOCK_HZ;
    }

    rcu_periph_clock_enable(RCU_GPIOB);
    s_half_period_cycles = SystemCoreClock / (clock_hz * 2U);
    if (s_half_period_cycles == 0U) {
        s_half_period_cycles = 1U;
    }
    s_next_edge_cycle = board_cycle_count();
    swclk_write(false);
    gpio_init(BOARD_TGT_SWCLK_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              BOARD_TGT_SWCLK_PIN);
    swdio_input();
}

void target_swd_configure(uint8_t idle_cycles, uint16_t retry_count,
                          uint8_t turnaround, bool data_phase)
{
    s_idle_cycles = idle_cycles;
    s_wait_retries = retry_count > TARGET_SWD_MAX_WAIT_RETRIES
                         ? TARGET_SWD_MAX_WAIT_RETRIES
                         : retry_count;
    s_turnaround =
        (turnaround >= 1U) && (turnaround <= 4U) ? turnaround : 1U;
    s_data_phase = data_phase;
}

void target_swd_disconnect(void)
{
    swclk_write(false);
    gpio_init(BOARD_TGT_SWCLK_PORT, GPIO_MODE_IN_FLOATING,
              GPIO_OSPEED_2MHZ, BOARD_TGT_SWCLK_PIN);
    swdio_input();
}

bool target_swd_connect(uint32_t *idcode)
{
    uint8_t index;
    uint32_t value = 0U;

    swdio_output();
    swdio_write(true);
    for (index = 0U; index < 56U; ++index) {
        clock_cycle();
    }
    write_bits(0xE79EU, 16U);
    swdio_write(true);
    for (index = 0U; index < 56U; ++index) {
        clock_cycle();
    }
    write_bits(0U, 8U);
    swdio_input();

    if (target_swd_transfer(0x02U, &value) != TARGET_SWD_ACK_OK) {
        return false;
    }
    if (idcode != NULL) {
        *idcode = value;
    }
    return true;
}

target_swd_ack_t target_swd_transfer(uint8_t request, uint32_t *data)
{
    target_swd_ack_t ack;
    uint32_t started_at = board_millis();
    uint16_t retries = 0U;

    do {
        ack = transfer_once(request, data);
        ++retries;
    } while ((ack == TARGET_SWD_ACK_WAIT) &&
             (retries <= s_wait_retries) &&
             ((uint32_t)(board_millis() - started_at) <
              TARGET_SWD_TRANSFER_BUDGET_MS) &&
             !s_abort_requested);
    return ack;
}

void target_swd_abort_request(void)
{
    s_abort_requested = true;
}

void target_swd_abort_clear(void)
{
    s_abort_requested = false;
}

bool target_swd_sequence(uint16_t bit_count, const uint8_t *data)
{
    uint16_t bit;

    if ((data == NULL) || (bit_count == 0U) || (bit_count > 488U)) {
        return false;
    }
    swdio_output();
    for (bit = 0U; bit < bit_count; ++bit) {
        write_bit((data[bit / 8U] >> (bit & 7U)) & 1U);
    }
    swdio_input();
    return true;
}

bool target_swd_sequence_transfer(const uint8_t *request,
                                  uint8_t request_length,
                                  uint8_t *response,
                                  uint8_t *response_length)
{
    uint16_t input_offset = 1U;
    uint16_t output_offset = 1U;
    uint8_t sequence;

    if ((request == NULL) || (request_length == 0U) ||
        (response == NULL) || (response_length == NULL)) {
        return false;
    }
    response[0] = 0U;
    for (sequence = 0U; sequence < request[0]; ++sequence) {
        uint8_t info;
        uint8_t byte_count;
        uint8_t bit_count;
        uint8_t bit;

        if (input_offset >= request_length) {
            return false;
        }
        info = request[input_offset++];
        bit_count = info & 0x3FU;
        if (bit_count == 0U) {
            bit_count = 64U;
        }
        byte_count = (uint8_t)((bit_count + 7U) / 8U);
        if ((info & 0x80U) != 0U) {
            if ((output_offset + byte_count) >
                SWD_SEQUENCE_MAX_RESPONSE) {
                return false;
            }
            memset(&response[output_offset], 0, byte_count);
            swdio_input();
            for (bit = 0U; bit < bit_count; ++bit) {
                response[output_offset + bit / 8U] |=
                    (uint8_t)(read_bit() << (bit & 7U));
            }
            output_offset = (uint8_t)(output_offset + byte_count);
        } else {
            if ((uint8_t)(request_length - input_offset) <
                byte_count) {
                return false;
            }
            swdio_output();
            for (bit = 0U; bit < bit_count; ++bit) {
                write_bit((request[input_offset + bit / 8U] >>
                           (bit & 7U)) &
                          1U);
            }
            input_offset = (uint8_t)(input_offset + byte_count);
        }
    }
    swdio_input();
    *response_length = (uint8_t)output_offset;
    return input_offset == request_length;
}

void target_swd_pins_set(uint8_t value, uint8_t select)
{
    if ((select & 0x01U) != 0U) {
        swclk_write((value & 0x01U) != 0U);
        gpio_init(BOARD_TGT_SWCLK_PORT, GPIO_MODE_OUT_PP,
                  GPIO_OSPEED_50MHZ, BOARD_TGT_SWCLK_PIN);
    }
    if ((select & 0x02U) != 0U) {
        swdio_write((value & 0x02U) != 0U);
        swdio_output();
    }
    if ((select & 0x80U) != 0U) {
        if ((value & 0x80U) != 0U) {
            gpio_init(BOARD_TGT_NRST_PORT, GPIO_MODE_IPU,
                      GPIO_OSPEED_2MHZ, BOARD_TGT_NRST_PIN);
        } else {
            GPIO_BC(BOARD_TGT_NRST_PORT) = BOARD_TGT_NRST_PIN;
            gpio_init(BOARD_TGT_NRST_PORT, GPIO_MODE_OUT_OD,
                      GPIO_OSPEED_2MHZ, BOARD_TGT_NRST_PIN);
        }
    }

}

uint8_t target_swd_pins_read(void)
{
    uint8_t value = 0U;

    if ((GPIO_ISTAT(BOARD_TGT_SWCLK_PORT) &
         BOARD_TGT_SWCLK_PIN) != 0U) {
        value |= 0x01U;
    }
    if ((GPIO_ISTAT(BOARD_TGT_SWDIO_PORT) &
         BOARD_TGT_SWDIO_PIN) != 0U) {
        value |= 0x02U;
    }
    if ((GPIO_ISTAT(BOARD_TGT_NRST_PORT) &
         BOARD_TGT_NRST_PIN) != 0U) {
        value |= 0x80U;
    }
    return value;
}

void target_swd_reset_pulse(uint32_t duration_ms)
{
    GPIO_BC(BOARD_TGT_NRST_PORT) = BOARD_TGT_NRST_PIN;
    gpio_init(BOARD_TGT_NRST_PORT, GPIO_MODE_OUT_OD, GPIO_OSPEED_2MHZ,
              BOARD_TGT_NRST_PIN);
    board_delay_ms(duration_ms);
    gpio_init(BOARD_TGT_NRST_PORT, GPIO_MODE_IPU, GPIO_OSPEED_2MHZ,
              BOARD_TGT_NRST_PIN);
}
