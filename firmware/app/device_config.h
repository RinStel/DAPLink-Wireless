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
#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "sx128x.h"

#define DEVICE_SYNC_CODE_LENGTH 16U

typedef enum {
    DEVICE_RATE_AUTO = 0,
    DEVICE_RATE_FIXED
} device_rate_mode_t;

typedef enum {
    DEVICE_MODE_WIRED = 0,
    DEVICE_MODE_WIRELESS_HOST,
    DEVICE_MODE_WIRELESS_SLAVE
} device_mode_t;

typedef struct {
    char sync_code[DEVICE_SYNC_CODE_LENGTH + 1U];
    uint32_t network_id;
    uint8_t radio_sync_word[5];
    device_mode_t device_mode;
    device_rate_mode_t rate_mode;
    sx128x_profile_t fixed_profile;
} device_config_t;

void device_config_init(void);
const device_config_t *device_config_get(void);
bool device_config_is_valid(const device_config_t *config);
bool device_config_set_sync_code(const char *sync_code);
bool device_config_set_rate(device_rate_mode_t mode,
                            sx128x_profile_t fixed_profile);
bool device_config_set_mode(device_mode_t mode);

bool device_config_apply(const char *sync_code,
                         device_mode_t device_mode,
                         device_rate_mode_t rate_mode,
                         sx128x_profile_t fixed_profile);
void device_config_button_cycle_rate(void);
void device_config_button_cycle_mode(void);

#endif
