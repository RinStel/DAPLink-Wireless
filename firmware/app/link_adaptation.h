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
#ifndef LINK_ADAPTATION_H
#define LINK_ADAPTATION_H

#include <stdbool.h>
#include <stdint.h>

#include "sx128x.h"

typedef struct {
    bool initialized;
    int16_t rssi_ewma_dbm_x2;
    uint16_t success_count;
    uint16_t failure_count;
    uint8_t upgrade_votes;
    uint8_t downgrade_votes;
    uint32_t last_change_ms;
    sx128x_profile_t current_profile;
} link_adaptation_t;

void link_adaptation_init(link_adaptation_t *state,
                          sx128x_profile_t initial_profile,
                          uint32_t now_ms);
void link_adaptation_record_success(link_adaptation_t *state,
                                    int16_t rssi_dbm_x2);
void link_adaptation_record_failure(link_adaptation_t *state);
sx128x_profile_t link_adaptation_recommend(link_adaptation_t *state,
                                           uint32_t now_ms);
void link_adaptation_profile_changed(link_adaptation_t *state,
                                     sx128x_profile_t profile,
                                     uint32_t now_ms);

#endif
