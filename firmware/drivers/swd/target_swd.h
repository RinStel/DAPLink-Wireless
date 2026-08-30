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
#ifndef TARGET_SWD_H
#define TARGET_SWD_H

#include <stdbool.h>
#include <stdint.h>

#define SWD_SEQUENCE_MAX_RESPONSE 60U
#define TARGET_SWD_DEFAULT_CLOCK_HZ 4000000U
#define TARGET_SWD_MIN_CLOCK_HZ     10000U
#define TARGET_SWD_MAX_CLOCK_HZ     4000000U
#define TARGET_SWD_CLOCK_IDLE_HIGH  1U
#define TARGET_SWD_SAMPLE_CLOCK_HIGH 0U
#define TARGET_SWDIO_CTL_INDEX      4U
#define TARGET_SWDIO_CTL_SHIFT      (4U * TARGET_SWDIO_CTL_INDEX)
#define TARGET_SWDIO_CTL_MASK       (0xFU << TARGET_SWDIO_CTL_SHIFT)

static inline uint32_t target_swd_normalize_clock(uint32_t clock_hz)
{
    if (clock_hz == 0U) {
        return TARGET_SWD_DEFAULT_CLOCK_HZ;
    }
    if (clock_hz < TARGET_SWD_MIN_CLOCK_HZ) {
        return TARGET_SWD_MIN_CLOCK_HZ;
    }
    if (clock_hz > TARGET_SWD_MAX_CLOCK_HZ) {
        return TARGET_SWD_MAX_CLOCK_HZ;
    }
    return clock_hz;
}

/* GD32 模式常量的高位还包含上下拉选择；GPIO_CTL1 只存低四位模式字段。
 * 移位前必须屏蔽高位，避免修改 PB12 时污染相邻 PB13 字段。 */
static inline uint32_t target_swd_swdio_ctl1_set_mode(uint32_t reg,
                                                       uint8_t mode)
{
    reg &= ~TARGET_SWDIO_CTL_MASK;
    reg |= ((uint32_t)(mode & 0x0FU) << TARGET_SWDIO_CTL_SHIFT);
    return reg;
}

typedef enum {
    TARGET_SWD_ACK_OK = 1,
    TARGET_SWD_ACK_WAIT = 2,
    TARGET_SWD_ACK_FAULT = 4,
    TARGET_SWD_ACK_PROTOCOL = 7,
    TARGET_SWD_ACK_PARITY = 8
} target_swd_ack_t;

typedef enum {
    TARGET_SWD_POLL_IDLE = 0,
    TARGET_SWD_POLL_BUSY,
    TARGET_SWD_POLL_DONE,
    TARGET_SWD_POLL_CANCELLED,
    TARGET_SWD_POLL_ERROR
} target_swd_poll_result_t;

typedef void (*target_swd_poll_hook_t)(void);

void target_swd_init(uint32_t clock_hz);
void target_swd_configure(uint8_t idle_cycles, uint16_t retry_count,
                          uint8_t turnaround, bool data_phase);
void target_swd_disconnect(void);
bool target_swd_connect(uint32_t *idcode);
target_swd_ack_t target_swd_transfer(uint8_t request, uint32_t *data);
bool target_swd_transfer_begin(uint8_t request, uint32_t *data);
target_swd_poll_result_t target_swd_transfer_poll(target_swd_ack_t *ack);
void target_swd_transfer_cancel(void);
void target_swd_poll_hook_set(target_swd_poll_hook_t hook);
void target_swd_abort_request(void);
void target_swd_abort_clear(void);
bool target_swd_sequence(uint16_t bit_count, const uint8_t *data);
bool target_swd_sequence_transfer(const uint8_t *request,
                                  uint8_t request_length,
                                  uint8_t *response,
                                  uint8_t *response_length);
void target_swd_pins_set(uint8_t value, uint8_t select);
uint8_t target_swd_pins_read(void);
void target_swd_reset_pulse(uint32_t duration_ms);

#endif
