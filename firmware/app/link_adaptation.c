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
#include "link_adaptation.h"

#define ADAPT_MIN_DWELL_MS          3000U
#define ADAPT_UPGRADE_VOTES         4U
#define ADAPT_DOWNGRADE_VOTES       2U
#define ADAPT_FAILURE_DOWNGRADE     3U
#define ADAPT_RSSI_HYSTERESIS_X2    8

typedef struct {
    sx128x_profile_t profile;
    int16_t enter_rssi_dbm_x2;
} profile_policy_t;

/* Ordered from highest throughput to highest robustness. */
static const profile_policy_t s_policy[] = {
    {SX128X_PROFILE_GFSK_2M, -108},
    {SX128X_PROFILE_FLRC_1M3, -132},
    {SX128X_PROFILE_GFSK_1M, -144},
    {SX128X_PROFILE_FLRC_650K, -170},
    {SX128X_PROFILE_GFSK_500K, INT16_MIN}
};

static uint8_t profile_index(sx128x_profile_t profile)
{
    uint8_t index;

    for (index = 0U; index < (uint8_t)(sizeof(s_policy) / sizeof(s_policy[0]));
         ++index) {
        if (s_policy[index].profile == profile) {
            return index;
        }
    }
    return (uint8_t)(sizeof(s_policy) / sizeof(s_policy[0]) - 1U);
}

void link_adaptation_init(link_adaptation_t *state,
                          sx128x_profile_t initial_profile,
                          uint32_t now_ms)
{
    state->initialized = false;
    state->rssi_ewma_dbm_x2 = INT16_MIN;
    state->success_count = 0U;
    state->failure_count = 0U;
    state->upgrade_votes = 0U;
    state->downgrade_votes = 0U;
    state->last_change_ms = now_ms;
    state->current_profile = initial_profile;
}

void link_adaptation_record_success(link_adaptation_t *state,
                                    int16_t rssi_dbm_x2)
{
    if (!state->initialized) {
        state->rssi_ewma_dbm_x2 = rssi_dbm_x2;
        state->initialized = true;
    } else {
        /* EWMA alpha = 1/4, retaining fractional 0.5 dB units. */
        state->rssi_ewma_dbm_x2 =
            (int16_t)((3 * state->rssi_ewma_dbm_x2 + rssi_dbm_x2) / 4);
    }
    if (state->success_count != UINT16_MAX) {
        ++state->success_count;
    }
    state->failure_count = 0U;
}

void link_adaptation_record_failure(link_adaptation_t *state)
{
    if (state->failure_count != UINT16_MAX) {
        ++state->failure_count;
    }
    state->success_count = 0U;
}

sx128x_profile_t link_adaptation_recommend(link_adaptation_t *state,
                                           uint32_t now_ms)
{
    uint8_t current = profile_index(state->current_profile);
    uint8_t target = current;

    if (state->failure_count >= ADAPT_FAILURE_DOWNGRADE) {
        target = current + 1U;
        if (target >= (uint8_t)(sizeof(s_policy) / sizeof(s_policy[0]))) {
            target = current;
        }
        state->downgrade_votes = ADAPT_DOWNGRADE_VOTES;
    } else if (state->initialized) {
        if ((current > 0U) &&
            (state->rssi_ewma_dbm_x2 >=
             s_policy[current - 1U].enter_rssi_dbm_x2 +
                 ADAPT_RSSI_HYSTERESIS_X2)) {
            if (state->upgrade_votes != UINT8_MAX) {
                ++state->upgrade_votes;
            }
            state->downgrade_votes = 0U;
            if (state->upgrade_votes >= ADAPT_UPGRADE_VOTES) {
                target = current - 1U;
            }
        } else if ((current + 1U <
                    (uint8_t)(sizeof(s_policy) / sizeof(s_policy[0]))) &&
                   (state->rssi_ewma_dbm_x2 <
                    s_policy[current].enter_rssi_dbm_x2 -
                        ADAPT_RSSI_HYSTERESIS_X2)) {
            if (state->downgrade_votes != UINT8_MAX) {
                ++state->downgrade_votes;
            }
            state->upgrade_votes = 0U;
            if (state->downgrade_votes >= ADAPT_DOWNGRADE_VOTES) {
                target = current + 1U;
            }
        } else {
            state->upgrade_votes = 0U;
            state->downgrade_votes = 0U;
        }
    }

    if ((target != current) &&
        ((uint32_t)(now_ms - state->last_change_ms) >= ADAPT_MIN_DWELL_MS)) {
        return s_policy[target].profile;
    }
    return state->current_profile;
}

void link_adaptation_profile_changed(link_adaptation_t *state,
                                     sx128x_profile_t profile,
                                     uint32_t now_ms)
{
    state->current_profile = profile;
    state->last_change_ms = now_ms;
    state->upgrade_votes = 0U;
    state->downgrade_votes = 0U;
    state->failure_count = 0U;
}
