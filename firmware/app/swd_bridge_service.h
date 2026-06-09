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
#ifndef SWD_BRIDGE_SERVICE_H
#define SWD_BRIDGE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "device_config.h"
#include "swd_tunnel.h"

void swd_bridge_service_init(void);
void swd_bridge_service_reset(void);
void swd_bridge_service_process(void);
bool swd_bridge_service_begin(device_mode_t mode,
                              const uint8_t *payload, uint8_t length);
bool swd_bridge_service_wireless_request(const uint8_t *payload,
                                         uint8_t length);
bool swd_bridge_service_wireless_response(const uint8_t *payload,
                                          uint8_t length);
bool swd_bridge_service_reply_take(uint8_t *payload, uint8_t *length);
bool swd_bridge_service_response_take(swd_tunnel_response_t *response);
void swd_bridge_service_repeat_request(void);
bool swd_bridge_service_cancel(uint8_t transaction_id);
bool swd_bridge_service_request_active(void);
uint32_t swd_bridge_service_cancellations(void);
uint32_t swd_bridge_service_stale_responses(void);

#endif
