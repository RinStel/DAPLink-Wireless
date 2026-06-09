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
#ifndef FREQUENCY_HOPPING_H
#define FREQUENCY_HOPPING_H

#include <stdbool.h>
#include <stdint.h>

#define FREQUENCY_HOPPING_CHANNEL_COUNT 16U

typedef struct {
    uint32_t seed;
    uint8_t penalty[FREQUENCY_HOPPING_CHANNEL_COUNT];
    uint8_t decay_counter;
} frequency_hopping_t;

void frequency_hopping_init(frequency_hopping_t *state,
                            uint32_t network_id);
uint8_t frequency_hopping_rendezvous(
    const frequency_hopping_t *state);
uint8_t frequency_hopping_select(const frequency_hopping_t *state,
                                 uint32_t token, uint8_t attempt,
                                 uint8_t excluded_channel);
uint32_t frequency_hopping_frequency_hz(uint8_t channel);
bool frequency_hopping_channel_valid(uint8_t channel);
void frequency_hopping_record_success(frequency_hopping_t *state,
                                      uint8_t channel);
void frequency_hopping_record_failure(frequency_hopping_t *state,
                                      uint8_t channel);

#endif
