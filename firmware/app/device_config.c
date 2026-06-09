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
#include "device_config.h"

#include <stddef.h>
#include <string.h>

#include "device_config_storage.h"

#define DEFAULT_SYNC_CODE "DAPLINKWIRELESS1"

static device_config_t s_config;

static bool sync_character_is_valid(char value)
{
    return ((value >= '0') && (value <= '9')) ||
           ((value >= 'A') && (value <= 'Z')) ||
           ((value >= 'a') && (value <= 'z'));
}

static bool sync_code_is_valid(const char *sync_code)
{
    size_t index;

    if (sync_code == NULL) {
        return false;
    }
    for (index = 0U; index < DEVICE_SYNC_CODE_LENGTH; ++index) {
        if ((sync_code[index] == '\0') ||
            !sync_character_is_valid(sync_code[index])) {
            return false;
        }
    }
    return sync_code[DEVICE_SYNC_CODE_LENGTH] == '\0';
}

static uint64_t fnv1a_64(const char *data, size_t length)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t index;

    for (index = 0U; index < length; ++index) {
        hash ^= (uint8_t)data[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void derive_network_values(void)
{
    uint64_t hash = fnv1a_64(s_config.sync_code, DEVICE_SYNC_CODE_LENGTH);

    s_config.network_id = (uint32_t)(hash ^ (hash >> 32));
    s_config.radio_sync_word[0] = (uint8_t)(hash >> 32);
    s_config.radio_sync_word[1] = (uint8_t)(hash >> 24);
    s_config.radio_sync_word[2] = (uint8_t)(hash >> 16);
    s_config.radio_sync_word[3] = (uint8_t)(hash >> 8);
    s_config.radio_sync_word[4] = (uint8_t)hash;

    /* Avoid weak all-zero/all-one sync patterns after hashing. */
    s_config.radio_sync_word[0] ^= 0xA7U;
    s_config.radio_sync_word[4] ^= 0x5DU;
}

void device_config_init(void)
{
    (void)device_config_set_sync_code(DEFAULT_SYNC_CODE);
    s_config.device_mode = DEVICE_MODE_WIRELESS_HOST;
    s_config.rate_mode = DEVICE_RATE_AUTO;
    s_config.fixed_profile = SX128X_PROFILE_GFSK_1M;
    (void)device_config_storage_load(&s_config);
}

const device_config_t *device_config_get(void)
{
    return &s_config;
}

bool device_config_is_valid(const device_config_t *config)
{
    return (config != NULL) &&
           sync_code_is_valid(config->sync_code) &&
           (config->device_mode <= DEVICE_MODE_WIRELESS_SLAVE) &&
           (config->rate_mode <= DEVICE_RATE_FIXED) &&
           (config->fixed_profile < SX128X_PROFILE_COUNT);
}

bool device_config_set_sync_code(const char *sync_code)
{
    if (!sync_code_is_valid(sync_code)) {
        return false;
    }

    memcpy(s_config.sync_code, sync_code, DEVICE_SYNC_CODE_LENGTH + 1U);
    derive_network_values();
    return true;
}

bool device_config_set_rate(device_rate_mode_t mode,
                            sx128x_profile_t fixed_profile)
{
    if ((mode > DEVICE_RATE_FIXED) ||
        (fixed_profile >= SX128X_PROFILE_COUNT)) {
        return false;
    }

    s_config.rate_mode = mode;
    s_config.fixed_profile = fixed_profile;
    return true;
}

bool device_config_set_mode(device_mode_t mode)
{
    if (mode > DEVICE_MODE_WIRELESS_SLAVE) {
        return false;
    }
    s_config.device_mode = mode;
    return true;
}

bool device_config_apply(const char *sync_code,
                         device_mode_t device_mode,
                         device_rate_mode_t rate_mode,
                         sx128x_profile_t fixed_profile)
{
    if (!sync_code_is_valid(sync_code) ||
        (device_mode > DEVICE_MODE_WIRELESS_SLAVE) ||
        (rate_mode > DEVICE_RATE_FIXED) ||
        (fixed_profile >= SX128X_PROFILE_COUNT)) {
        return false;
    }

    memcpy(s_config.sync_code, sync_code, DEVICE_SYNC_CODE_LENGTH + 1U);
    derive_network_values();
    s_config.device_mode = device_mode;
    s_config.rate_mode = rate_mode;
    s_config.fixed_profile = fixed_profile;
    return true;
}

void device_config_button_cycle_rate(void)
{
    if (s_config.rate_mode == DEVICE_RATE_AUTO) {
        s_config.rate_mode = DEVICE_RATE_FIXED;
        s_config.fixed_profile = SX128X_PROFILE_GFSK_1M;
        return;
    }

    s_config.fixed_profile =
        (sx128x_profile_t)(s_config.fixed_profile + 1U);
    if (s_config.fixed_profile >= SX128X_PROFILE_COUNT) {
        s_config.rate_mode = DEVICE_RATE_AUTO;
        s_config.fixed_profile = SX128X_PROFILE_GFSK_1M;
    }
}

void device_config_button_cycle_mode(void)
{
    s_config.device_mode =
        (device_mode_t)(s_config.device_mode + 1U);
    if (s_config.device_mode > DEVICE_MODE_WIRELESS_SLAVE) {
        s_config.device_mode = DEVICE_MODE_WIRED;
    }
}
