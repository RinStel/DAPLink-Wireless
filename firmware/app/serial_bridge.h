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
#ifndef SERIAL_BRIDGE_H
#define SERIAL_BRIDGE_H

#include <stdbool.h>

#include "swd_tunnel.h"

typedef struct {
    uint32_t radio_recoveries;
    uint32_t swd_cancellations;
    uint32_t stale_swd_responses;
    uint32_t uart_rx_overruns;
    uint32_t radio_timeouts;
    uint32_t invalid_radio_frames;
    uint32_t peer_session_changes;
    int16_t remote_rssi_dbm_x2;
    uint8_t device_mode;
    uint8_t retries;
    uint8_t radio_profile;
    uint8_t profile_switches;
    uint8_t radio_channel;
    uint8_t channel_switches;
    uint8_t remote_error_status;
    uint8_t remote_tx_rx_status;
    uint8_t remote_sync_status;
    bool remote_metrics_valid;
    bool radio_ready;
    bool error;
    bool swd_request_active;
} serial_bridge_status_t;

bool serial_bridge_init(void);
bool serial_bridge_apply_config(void);
void serial_bridge_process(void);
bool serial_bridge_has_error(void);
bool serial_bridge_activity_led(void);
bool serial_bridge_swd_connect(uint8_t transaction_id);
bool serial_bridge_swd_disconnect(uint8_t transaction_id);
bool serial_bridge_swd_reset(uint8_t transaction_id);
bool serial_bridge_swd_sequence(uint8_t transaction_id,
                                uint16_t bit_count,
                                const uint8_t *data);
bool serial_bridge_swd_sequence_io(uint8_t transaction_id,
                                   const uint8_t *request,
                                   uint8_t request_length);
bool serial_bridge_swd_clock(uint8_t transaction_id, uint32_t clock_hz);
bool serial_bridge_swd_configure(uint8_t transaction_id,
                                 uint8_t idle_cycles,
                                 uint16_t retry_count,
                                 uint16_t match_retry,
                                 uint8_t turnaround,
                                 bool data_phase);
bool serial_bridge_swd_pins(uint8_t transaction_id, uint8_t value,
                            uint8_t select, uint32_t wait_us);
bool serial_bridge_swd_transfers(uint8_t transaction_id,
                                 const swd_tunnel_transfer_t *transfers,
                                 uint8_t count);
void serial_bridge_swd_cancel(uint8_t transaction_id);
bool serial_bridge_swd_response_take(swd_tunnel_response_t *response);
void serial_bridge_status_get(serial_bridge_status_t *status);

#endif
