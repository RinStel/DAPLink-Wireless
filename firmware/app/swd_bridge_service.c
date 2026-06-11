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
#include "swd_bridge_service.h"

#include <stddef.h>
#include <string.h>

typedef enum {
    SWD_OWNER_NONE = 0,
    SWD_OWNER_WIRED_HOST,
    SWD_OWNER_WIRELESS_SLAVE
} swd_owner_t;

static swd_owner_t s_owner;
static bool s_request_active;
static bool s_response_ready;
static bool s_reply_ready;
static uint8_t s_expected_transaction;
static swd_tunnel_response_t s_response;
static uint8_t s_reply[SWD_TUNNEL_MAX_PAYLOAD];
static uint8_t s_reply_length;
static uint32_t s_cancellations;
static uint32_t s_stale_responses;

void swd_bridge_service_init(void)
{
    s_cancellations = 0U;
    s_stale_responses = 0U;
    swd_bridge_service_reset();
}

void swd_bridge_service_reset(void)
{
    swd_tunnel_cancel();
    s_owner = SWD_OWNER_NONE;
    s_request_active = false;
    s_response_ready = false;
    s_reply_ready = false;
    s_reply_length = 0U;
}

void swd_bridge_service_process(void)
{
    uint8_t payload[SWD_TUNNEL_MAX_PAYLOAD];
    uint8_t length;

    swd_tunnel_process();
    if (!swd_tunnel_response_take(payload, &length)) {
        return;
    }
    if (s_owner == SWD_OWNER_WIRELESS_SLAVE) {
        memcpy(s_reply, payload, length);
        s_reply_length = length;
        s_reply_ready = true;
    } else if ((s_owner == SWD_OWNER_WIRED_HOST) &&
               swd_tunnel_decode_response(payload, length, &s_response)) {
        s_response_ready = true;
        s_request_active = false;
    }
    s_owner = SWD_OWNER_NONE;
}

bool swd_bridge_service_begin(device_mode_t mode,
                              const uint8_t *payload, uint8_t length)
{
    if ((payload == NULL) || (length < 2U) || s_request_active) {
        return false;
    }
    s_expected_transaction = payload[1];
    s_request_active = true;
    s_response_ready = false;
    if (mode == DEVICE_MODE_WIRED) {
        if ((s_owner != SWD_OWNER_NONE) ||
            !swd_tunnel_submit(payload, length)) {
            s_request_active = false;
            return false;
        }
        s_owner = SWD_OWNER_WIRED_HOST;
        return true;
    }
    if (mode != DEVICE_MODE_WIRELESS_HOST) {
        s_request_active = false;
        return false;
    }
    return true;
}

bool swd_bridge_service_wireless_request(const uint8_t *payload,
                                         uint8_t length)
{
    if ((s_owner != SWD_OWNER_NONE) || s_reply_ready ||
        !swd_tunnel_submit(payload, length)) {
        return false;
    }
    s_expected_transaction = payload[1];
    s_owner = SWD_OWNER_WIRELESS_SLAVE;
    return true;
}

bool swd_bridge_service_wireless_response(const uint8_t *payload,
                                          uint8_t length)
{
    swd_tunnel_response_t response;

    if (!swd_tunnel_decode_response(payload, length, &response)) {
        return false;
    }
    if (s_request_active &&
        (response.transaction_id == s_expected_transaction)) {
        s_response = response;
        s_response_ready = true;
        s_request_active = false;
    } else {
        ++s_stale_responses;
    }
    return true;
}

bool swd_bridge_service_wireless_abort(uint8_t transaction_id)
{
    if ((s_owner != SWD_OWNER_WIRELESS_SLAVE) ||
        (transaction_id != s_expected_transaction)) {
        return false;
    }
    swd_tunnel_cancel();
    s_owner = SWD_OWNER_NONE;
    s_reply_ready = false;
    s_reply_length = 0U;
    ++s_cancellations;
    return true;
}

bool swd_bridge_service_reply_take(uint8_t *payload, uint8_t *length)
{
    if ((payload == NULL) || (length == NULL) || !s_reply_ready) {
        return false;
    }
    memcpy(payload, s_reply, s_reply_length);
    *length = s_reply_length;
    s_reply_ready = false;
    return true;
}

bool swd_bridge_service_response_take(swd_tunnel_response_t *response)
{
    if ((response == NULL) || !s_response_ready) {
        return false;
    }
    *response = s_response;
    s_response_ready = false;
    return true;
}

void swd_bridge_service_repeat_request(void)
{
    if ((s_owner == SWD_OWNER_NONE) && !s_reply_ready &&
        (s_reply_length != 0U)) {
        s_reply_ready = true;
    }
}

bool swd_bridge_service_cancel(uint8_t transaction_id)
{
    if (!s_request_active ||
        (transaction_id != s_expected_transaction)) {
        return false;
    }
    s_request_active = false;
    s_response_ready = false;
    ++s_cancellations;
    if (s_owner == SWD_OWNER_WIRED_HOST) {
        swd_tunnel_cancel();
        s_owner = SWD_OWNER_NONE;
    }
    return true;
}

bool swd_bridge_service_request_active(void)
{
    return s_request_active;
}

uint32_t swd_bridge_service_cancellations(void)
{
    return s_cancellations;
}

uint32_t swd_bridge_service_stale_responses(void)
{
    return s_stale_responses;
}
