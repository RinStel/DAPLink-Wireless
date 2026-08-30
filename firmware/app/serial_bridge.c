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
#include "serial_bridge.h"

#include <string.h>

#include "board.h"
#include "device_config.h"
#include "frequency_hopping.h"
#include "link_adaptation.h"
#include "radio_hal.h"
#include "radio_protocol.h"
#include "radio_window.h"
#include "serial_service.h"
#include "serial_bridge_scheduler.h"
#include "sx128x.h"
#include "swd_bridge_service.h"
#include "swd_tunnel.h"
#include "dap_diagnostics.h"
#include "target_swd.h"

/* 本模块负责无线可靠传输、会话校验、频道/Profile 自适应和 SWD 所有权。
 * 所有状态迁移由主循环推进，射频回调只通过 radio_hal 提供硬件状态。 */
#define BRIDGE_HEADER_SIZE          RADIO_PROTOCOL_HEADER_SIZE
#define BRIDGE_PAYLOAD_SIZE         RADIO_PROTOCOL_PAYLOAD_SIZE
#define BRIDGE_FRAME_SIZE           RADIO_PROTOCOL_FRAME_SIZE
#define BRIDGE_ACK_TIMEOUT_MS       120U
#define BRIDGE_SWD_RESPONSE_TIMEOUT_MS 500U
#define BRIDGE_ACTIVITY_MS          80U
#define BRIDGE_MAX_RETRIES          5U
#define BRIDGE_RECOVERY_DELAY_MS    250U
#define BRIDGE_PROFILE_TRIAL_MS     300U
#define BRIDGE_CHANNEL_TRIAL_MS     360U
#define BRIDGE_RENDEZVOUS_MS        500U
#define BRIDGE_CHANNEL_SCAN_START_MS 180U
#define BRIDGE_CHANNEL_SCAN_DWELL_MS 35U
#define BRIDGE_RECOVERY_CYCLE_MS     500U
#define BRIDGE_RECOVERY_HOME_MS      300U
#define BRIDGE_HOP_INTERVAL          32U
#define BRIDGE_RENDEZVOUS_PROFILE   SX128X_PROFILE_GFSK_1M

#define BRIDGE_FRAME_DATA            RADIO_FRAME_DATA
#define BRIDGE_FRAME_ACK             RADIO_FRAME_ACK
#define BRIDGE_FRAME_LINE_CODING     RADIO_FRAME_LINE_CODING
#define BRIDGE_FRAME_SWD_COMMAND    RADIO_FRAME_SWD_COMMAND
#define BRIDGE_FRAME_SWD_COMMAND_RESPONSE RADIO_FRAME_SWD_COMMAND_RESPONSE
#define BRIDGE_FRAME_PROFILE_SWITCH  RADIO_FRAME_PROFILE_SWITCH
#define BRIDGE_FRAME_PROFILE_CONFIRM RADIO_FRAME_PROFILE_CONFIRM
#define BRIDGE_FRAME_SESSION_START   RADIO_FRAME_SESSION_START
#define BRIDGE_FRAME_HOP_SWITCH      RADIO_FRAME_HOP_SWITCH
#define BRIDGE_FRAME_HOP_CONFIRM     RADIO_FRAME_HOP_CONFIRM
#define BRIDGE_FRAME_SWD_ABORT       RADIO_FRAME_SWD_ABORT

typedef radio_frame_type_t bridge_frame_type_t;

typedef enum {
    TX_NONE = 0,
    TX_RELIABLE,
    TX_ACK
} tx_kind_t;

static uint8_t s_pending_frame[BRIDGE_FRAME_SIZE];
static uint8_t s_pending_length;
static uint32_t s_pending_sequence;
static uint32_t s_next_sequence;
static uint32_t s_local_session;
static uint32_t s_session_generation;
static radio_frame_key_t s_last_rx_key;
static uint32_t s_deadline;
static uint32_t s_activity_until;
static uint32_t s_recover_at;
static uint8_t s_retries;
static bool s_pending;
static bool s_waiting_ack;
static bool s_radio_ready;
static bool s_error;
static tx_kind_t s_tx_kind;
static uint32_t s_radio_recoveries;
static uint32_t s_radio_timeouts;
static uint32_t s_invalid_radio_frames;
static uint32_t s_peer_session_changes;
static uint32_t s_remote_session;
static bool s_remote_session_valid;
static link_adaptation_t s_link_adaptation;
static sx128x_packet_status_t s_last_remote_metrics;
static bool s_remote_metrics_valid;
static bool s_switch_after_ack;
static bool s_profile_trial;
static sx128x_profile_t s_profile_before_trial;
static sx128x_profile_t s_profile_after_ack;
static uint32_t s_profile_trial_deadline;
static uint32_t s_last_valid_rx_ms;
static uint8_t s_profile_switches;
static bool s_session_announce_pending;
static frequency_hopping_t s_frequency_hopping;
static uint8_t s_current_channel;
static uint8_t s_channel_before_trial;
static uint8_t s_channel_after_ack;
static uint8_t s_channel_switches;
static uint8_t s_hop_success_count;
static uint32_t s_hop_generation;
static uint32_t s_channel_trial_deadline;
static uint32_t s_channel_scan_at;
static bool s_channel_switch_after_ack;
static bool s_channel_trial;
static bool s_hop_request_pending;
static bool s_swd_abort_pending;
static bool s_swd_abort_active;
static uint8_t s_swd_abort_transaction;
static radio_window_t s_data_tx_window;
static radio_window_t s_data_rx_window;
static uint32_t s_data_tx_sequence;

static uint32_t local_session_get(void)
{
    return s_local_session;
}

static void local_session_refresh(void)
{
    ++s_session_generation;
    s_local_session = board_device_id_hash() ^
                      (s_session_generation * 2654435761U) ^
                      board_cycle_count() ^ 0xA5C39E71U;
    if (s_local_session == 0U) {
        s_local_session = 1U;
    }
}

static void activity_signal(void)
{
    s_activity_until = board_millis() + BRIDGE_ACTIVITY_MS;
}

static void valid_rx_mark(void)
{
    s_last_valid_rx_ms = board_millis();
    s_channel_scan_at =
        s_last_valid_rx_ms + BRIDGE_CHANNEL_SCAN_START_MS;
}

static bool remote_session_accept(uint32_t session,
                                  bool session_start)
{
    if (!s_remote_session_valid) {
        s_remote_session = session;
        s_remote_session_valid = true;
        return true;
    }
    if (session == s_remote_session) {
        return true;
    }
    if (!session_start) {
        return false;
    }
    s_remote_session = session;
    ++s_peer_session_changes;
    return true;
}

static uint8_t frame_build(uint8_t *frame, bridge_frame_type_t type,
                           uint32_t sequence, const uint8_t *payload,
                           uint8_t payload_length)
{
    const device_config_t *config = device_config_get();

    return radio_protocol_build(frame, type, config->network_id,
                                local_session_get(), sequence,
                                payload, payload_length);
}

static bool frame_valid(const uint8_t *frame, uint8_t length)
{
    radio_frame_view_t view;

    return radio_protocol_parse(frame, length,
                                device_config_get()->network_id,
                                &view);
}

static bool radio_start_receive(void)
{
    if (sx128x_start_rx(0U) != SX128X_RESULT_OK) {
        return false;
    }
    return true;
}

static bool packet_status_read(sx128x_packet_status_t *status)
{
    if (status == NULL) {
        return false;
    }
    memset(status, 0, sizeof(*status));
    /* 固定 profile 不参与速率决策，ACK 也使用紧凑布局；省略一次
     * GET_PACKET_STATUS SPI 事务可降低每个 FLRC 包的接收尾延迟。 */
    if (!serial_bridge_packet_status_required(
            device_config_get()->rate_mode == DEVICE_RATE_AUTO)) {
        return true;
    }
    return sx128x_get_packet_status(status) == SX128X_RESULT_OK;
}

static bool radio_channel_set(uint8_t channel, bool start_receive)
{
    if (!frequency_hopping_channel_valid(channel) ||
        (sx128x_standby() != SX128X_RESULT_OK) ||
        (sx128x_set_frequency(
             frequency_hopping_frequency_hz(channel)) !=
         SX128X_RESULT_OK)) {
        return false;
    }
    s_current_channel = channel;
    return !start_receive || radio_start_receive();
}

static bool frame_transmit(const uint8_t *frame, uint8_t length,
                           tx_kind_t kind)
{
    /* 同时只能有一个无线发送。TX_DONE 清除 s_tx_kind，并按当前切换状态
     * 恢复对应频道和 profile 的接收窗口，然后才设置 ACK 截止时间。 */
    if (s_tx_kind != TX_NONE) {
        return false;
    }
    if (kind == TX_ACK) {
        /* 对端在自己的 TX_DONE 中立即重开接收，只需覆盖那段 SPI 序列
         * （约 15 字节 + BUSY 握手）。毫秒级阻塞会直接吃掉烧录吞吐。 */
        board_delay_us(serial_bridge_ack_turnaround_delay_us());
    }
    if (sx128x_start_tx(frame, length) != SX128X_RESULT_OK) {
        return false;
    }
    DAP_DIAG(rf_tx_start(length, (kind == TX_RELIABLE) && (s_retries != 0U)));
    s_tx_kind = kind;
    activity_signal();
    return true;
}

static bool data_frame_transmit(void)
{
    uint32_t sequence;
    uint8_t frame[BRIDGE_FRAME_SIZE];
    uint8_t index;

    if (s_tx_kind != TX_NONE || s_pending ||
        !radio_window_tx_due(&s_data_tx_window, board_millis(),
                             BRIDGE_ACK_TIMEOUT_MS, &sequence)) {
        return false;
    }
    for (index = 0U; index < RADIO_WINDOW_SIZE; ++index) {
        const radio_window_slot_t *slot = &s_data_tx_window.tx[index];

        if (slot->active && (slot->sequence == sequence)) {
            uint8_t length = frame_build(frame, BRIDGE_FRAME_DATA,
                                         sequence, slot->payload,
                                         slot->length);

            if ((length == 0U) ||
                !frame_transmit(frame, length, TX_RELIABLE)) {
                return false;
            }
            (void)radio_window_tx_mark_sent(&s_data_tx_window,
                                            sequence, board_millis());
            return true;
        }
    }
    return false;
}

static bool swd_request_frame_type(bridge_frame_type_t type)
{
    return (type == BRIDGE_FRAME_SWD_COMMAND) ||
           (type == RADIO_FRAME_SWD_BLOCK) ||
           (type == RADIO_FRAME_SWD_BURST);
}

static void reliable_queue(bridge_frame_type_t type,
                           const uint8_t *payload, uint8_t length)
{
    /* 可靠帧保留在 s_pending_frame 中，直到收到匹配 ACK 或达到有限重试和
     * 恢复策略的上限。 */
    if (s_pending || (length > BRIDGE_PAYLOAD_SIZE)) {
        return;
    }
    s_pending_sequence = ++s_next_sequence;
    s_pending_length = frame_build(s_pending_frame, type,
                                   s_pending_sequence, payload, length);
    s_retries = 0U;
    s_pending = true;
    s_waiting_ack = !serial_bridge_swd_request_ack_enabled() &&
                    swd_request_frame_type(type);
    if (s_waiting_ack) {
        s_deadline = board_millis() + BRIDGE_SWD_RESPONSE_TIMEOUT_MS;
    }
}

static bool ack_send(uint32_t sequence,
                     const sx128x_packet_status_t *status,
                     bool compact_requested)
{
    uint8_t frame[BRIDGE_FRAME_SIZE];
    uint8_t payload[RADIO_PROTOCOL_ACK_PAYLOAD_SIZE];
    radio_protocol_ack_t ack;
    bool hop_after_ack = false;
    bool compact = compact_requested &&
                   (device_config_get()->rate_mode != DEVICE_RATE_AUTO) &&
                   !s_hop_request_pending;
    uint8_t payload_length = compact
                                 ? RADIO_PROTOCOL_ACK_COMPACT_PAYLOAD_SIZE
                                 : RADIO_PROTOCOL_ACK_PAYLOAD_SIZE;
    uint8_t length;

    radio_window_rx_ack(&s_data_rx_window, &ack.ack_next, &ack.bitmap);
    ack.flags = 0U;
    ack.next_channel = s_current_channel;
    ack.rssi_dbm_x2 = status->rssi_dbm_x2;
    ack.error_status = status->error_status;
    ack.tx_rx_status = status->tx_rx_status;
    ack.sync_address_status = status->sync_address_status;
    ack.profile = (uint8_t)sx128x_get_profile();
    ack.current_channel = s_current_channel;
    if (s_hop_request_pending) {
        ack.flags |= RADIO_PROTOCOL_ACK_FLAG_HOP_VALID;
        ack.next_channel = frequency_hopping_select(
            &s_frequency_hopping, ++s_hop_generation, 0U,
            s_current_channel);
        hop_after_ack = serial_bridge_hop_after_ack(
            true, s_current_channel, ack.next_channel);
    }
    if (compact) {
        if (!radio_protocol_ack_encode_compact(
                payload, payload_length, &ack)) {
            return false;
        }
    } else if (!radio_protocol_ack_encode(payload, sizeof(payload), &ack)) {
        return false;
    }
    length = frame_build(frame, BRIDGE_FRAME_ACK, sequence, payload,
                         payload_length);

    if (!frame_transmit(frame, length, TX_ACK)) {
        return false;
    }
    if (hop_after_ack) {
        s_channel_after_ack = ack.next_channel;
        s_channel_switch_after_ack = true;
    }
    s_hop_request_pending = false;
    return true;
}

static bool swd_request_ack_allowed(bridge_frame_type_t type)
{
    if (serial_bridge_swd_request_ack_enabled()) {
        return true;
    }
    return (type != BRIDGE_FRAME_SWD_COMMAND) &&
           (type != RADIO_FRAME_SWD_BLOCK) &&
           (type != RADIO_FRAME_SWD_BURST);
}

static bool radio_configure(bool start_new_session)
{
    const device_config_t *config = device_config_get();
    sx128x_profile_t profile = config->rate_mode == DEVICE_RATE_FIXED
                                   ? config->fixed_profile
                                   : SX128X_PROFILE_GFSK_1M;
    uint8_t channel;

    frequency_hopping_init(&s_frequency_hopping, config->network_id);
    channel = frequency_hopping_rendezvous(&s_frequency_hopping);
    if ((radio_hal_init() != RADIO_RESULT_OK) ||
        (sx128x_init_gfsk() != SX128X_RESULT_OK) ||
        (sx128x_set_network_sync(config->radio_sync_word) !=
         SX128X_RESULT_OK) ||
        (sx128x_set_profile(profile) != SX128X_RESULT_OK) ||
        !radio_channel_set(channel, true)) {
        return false;
    }
    link_adaptation_init(&s_link_adaptation, profile, board_millis());
    if (start_new_session) {
        local_session_refresh();
        s_remote_session = 0U;
        s_remote_session_valid = false;
        memset(&s_last_rx_key, 0, sizeof(s_last_rx_key));
        s_session_announce_pending = true;
    }
    s_last_valid_rx_ms = board_millis();
    s_profile_trial = false;
    s_switch_after_ack = false;
    s_channel_trial = false;
    s_channel_switch_after_ack = false;
    s_hop_request_pending = false;
    s_hop_success_count = 0U;
    s_channel_scan_at =
        board_millis() + BRIDGE_CHANNEL_SCAN_START_MS;
    return true;
}

static void radio_fail(void)
{
    radio_hal_frontend_set(RADIO_FRONTEND_STANDBY);
    s_radio_ready = false;
    s_error = true;
    s_tx_kind = TX_NONE;
    s_waiting_ack = false;
    s_recover_at = board_millis() + BRIDGE_RECOVERY_DELAY_MS;
}

static bool frame_type_is_business(bridge_frame_type_t type)
{
    return (type == BRIDGE_FRAME_DATA) ||
           (type == BRIDGE_FRAME_LINE_CODING) ||
           (type == BRIDGE_FRAME_SWD_COMMAND) ||
           (type == BRIDGE_FRAME_SWD_COMMAND_RESPONSE) ||
           (type == RADIO_FRAME_SWD_BLOCK) ||
           (type == RADIO_FRAME_SWD_BLOCK_RESPONSE) ||
           (type == RADIO_FRAME_SWD_BURST) ||
           (type == RADIO_FRAME_SWD_BURST_RESPONSE);
}

static bool frame_type_allows_retry_hop(bridge_frame_type_t type)
{
    return frame_type_is_business(type) ||
           (type == BRIDGE_FRAME_SESSION_START);
}

static uint8_t recovery_channel_get(uint32_t now_ms)
{
    uint32_t elapsed = now_ms - s_last_valid_rx_ms;
    uint32_t phase = elapsed % BRIDGE_RECOVERY_CYCLE_MS;
    uint32_t cycle = elapsed / BRIDGE_RECOVERY_CYCLE_MS;
    uint8_t rendezvous =
        frequency_hopping_rendezvous(&s_frequency_hopping);

    if (phase < BRIDGE_RECOVERY_HOME_MS) {
        return rendezvous;
    }
    return frequency_hopping_select(
        &s_frequency_hopping, device_config_get()->network_id,
        (uint8_t)(cycle * 6U +
                  (phase - BRIDGE_RECOVERY_HOME_MS) /
                      BRIDGE_CHANNEL_SCAN_DWELL_MS),
        rendezvous);
}

static void frame_deliver(const uint8_t *frame, uint8_t frame_length,
                          const sx128x_packet_status_t *rx_status)
{
    const device_config_t *config = device_config_get();
    radio_frame_view_t view;
    radio_frame_key_t key;
    radio_frame_type_t type;
    uint32_t sequence;
    const uint8_t *payload;
    uint8_t length;
    bool duplicate;

    /* 分发前先解析并生成帧键。帧键抑制可靠帧重复处理，但允许重复发送 ACK。 */
    if (!radio_protocol_parse(frame, frame_length,
                              config->network_id, &view)) {
        return;
    }
    radio_protocol_key_get(&view, &key);
    type = view.type;
    sequence = view.sequence;
    payload = view.payload;
    length = view.payload_length;
    /* v2 将跳频目标合并到 ACK；独立 HOP_SWITCH/HOP_CONFIRM 不再是运行时
     * 接受的控制路径。 */
    if ((type == BRIDGE_FRAME_HOP_SWITCH) ||
        (type == BRIDGE_FRAME_HOP_CONFIRM)) {
        return;
    }
    if (type == BRIDGE_FRAME_SESSION_START) {
        if (length != 0U) {
            return;
        }
        if (!remote_session_accept(view.session, true)) {
            return;
        }
        if ((config->device_mode == DEVICE_MODE_WIRELESS_SLAVE) &&
            (s_last_rx_key.session != view.session)) {
            s_pending = false;
            s_waiting_ack = false;
            s_retries = 0U;
            s_switch_after_ack = false;
            s_profile_trial = false;
            s_channel_switch_after_ack = false;
            s_channel_trial = false;
            swd_bridge_service_reset();
        }
        s_last_rx_key = key;
        valid_rx_mark();
        if (!ack_send(sequence, rx_status, false)) {
            radio_fail();
        }
        return;
    }
    if (type == BRIDGE_FRAME_ACK) {
        radio_protocol_ack_t ack;
        uint8_t data_active_before;
        bool compact_ack;

        if (!radio_protocol_ack_decode(payload, length, &ack) ||
            ((length == RADIO_PROTOCOL_ACK_COMPACT_PAYLOAD_SIZE) &&
             (device_config_get()->rate_mode == DEVICE_RATE_AUTO)) ||
            (ack.profile >= SX128X_PROFILE_COUNT) ||
            !frequency_hopping_channel_valid(ack.current_channel) ||
            !remote_session_accept(view.session, false)) {
            return;
        }
        compact_ack = length == RADIO_PROTOCOL_ACK_COMPACT_PAYLOAD_SIZE;
        valid_rx_mark();
        if (!compact_ack) {
            s_last_remote_metrics.rssi_dbm_x2 = ack.rssi_dbm_x2;
            s_last_remote_metrics.error_status = ack.error_status;
            s_last_remote_metrics.tx_rx_status = ack.tx_rx_status;
            s_last_remote_metrics.sync_address_status = ack.sync_address_status;
            s_remote_metrics_valid = true;
        }
        data_active_before = radio_window_tx_active(&s_data_tx_window);
        radio_window_tx_ack(&s_data_tx_window, ack.ack_next, ack.bitmap);
        if ((ack.flags & RADIO_PROTOCOL_ACK_FLAG_HOP_VALID) != 0U &&
            frequency_hopping_channel_valid(ack.next_channel) &&
            (ack.next_channel != s_current_channel)) {
            if (!radio_channel_set(ack.next_channel, true)) {
                radio_fail();
                return;
            }
            ++s_channel_switches;
        }
        frequency_hopping_record_success(&s_frequency_hopping,
                                         s_current_channel);
        if (device_config_get()->rate_mode == DEVICE_RATE_AUTO) {
            link_adaptation_record_success(
                &s_link_adaptation, s_last_remote_metrics.rssi_dbm_x2);
        }
        if (device_config_get()->device_mode == DEVICE_MODE_WIRELESS_HOST &&
            serial_bridge_periodic_hop_progress(
                data_active_before,
                radio_window_tx_active(&s_data_tx_window)) &&
            ++s_hop_success_count >= BRIDGE_HOP_INTERVAL) {
            s_hop_success_count = 0U;
            s_hop_request_pending = true;
        }
        if (s_pending && (sequence == s_pending_sequence)) {
            bridge_frame_type_t pending_type =
                (bridge_frame_type_t)s_pending_frame[3];
            bool swd_pending_response =
                (pending_type == BRIDGE_FRAME_SWD_COMMAND) ||
                (pending_type == RADIO_FRAME_SWD_BLOCK) ||
                (pending_type == RADIO_FRAME_SWD_BURST);

            if (swd_pending_response) {
                DAP_DIAG(request_ack());
            }

            /* SWD ACK 只确认请求到达；必须继续等待匹配的隧道响应。 */
            if (!swd_pending_response) {
                s_pending = false;
                s_waiting_ack = false;
                s_retries = 0U;
            } else {
                /* 请求帧已经到达远端，但隧道响应仍可能丢失。缩短等待窗口，
                 * 让同一事务在 CMSIS-DAP 总超时内完成多次端到端重试。 */
                s_waiting_ack = true;
                s_deadline = board_millis() +
                             BRIDGE_SWD_RESPONSE_TIMEOUT_MS;
            }
            if (pending_type == BRIDGE_FRAME_PROFILE_SWITCH) {
                sx128x_profile_t profile =
                    (sx128x_profile_t)
                        s_pending_frame[BRIDGE_HEADER_SIZE];
                uint8_t confirm_profile = (uint8_t)profile;

                s_profile_before_trial = sx128x_get_profile();
                if ((profile < SX128X_PROFILE_COUNT) &&
                    (sx128x_set_profile(profile) ==
                     SX128X_RESULT_OK) &&
                    radio_start_receive()) {
                    link_adaptation_profile_changed(
                        &s_link_adaptation, profile,
                        board_millis());
                    ++s_profile_switches;
                    reliable_queue(BRIDGE_FRAME_PROFILE_CONFIRM,
                                   &confirm_profile, 1U);
                } else {
                    radio_fail();
                }
            } else if (pending_type == BRIDGE_FRAME_HOP_SWITCH) {
                uint8_t channel =
                    s_pending_frame[BRIDGE_HEADER_SIZE];

                s_channel_before_trial = s_current_channel;
                if (radio_channel_set(channel, true)) {
                    ++s_channel_switches;
                    s_channel_trial = true;
                    s_channel_trial_deadline =
                        board_millis() + BRIDGE_CHANNEL_TRIAL_MS;
                    reliable_queue(BRIDGE_FRAME_HOP_CONFIRM,
                                   &channel, 1U);
                } else {
                    radio_fail();
                }
            } else if (pending_type == BRIDGE_FRAME_HOP_CONFIRM) {
                s_channel_trial = false;
            } else if (pending_type == BRIDGE_FRAME_SWD_ABORT) {
                s_swd_abort_active = false;
            }
        }
        return;
    }
    if (!remote_session_accept(view.session, false)) {
        return;
    }
    if ((type != BRIDGE_FRAME_DATA) &&
        (type != BRIDGE_FRAME_LINE_CODING) &&
        (type != BRIDGE_FRAME_SWD_COMMAND) &&
        (type != BRIDGE_FRAME_SWD_COMMAND_RESPONSE) &&
        (type != RADIO_FRAME_SWD_BLOCK) &&
        (type != RADIO_FRAME_SWD_BLOCK_RESPONSE) &&
        (type != RADIO_FRAME_SWD_BURST) &&
        (type != RADIO_FRAME_SWD_BURST_RESPONSE) &&
        (type != BRIDGE_FRAME_PROFILE_SWITCH) &&
        (type != BRIDGE_FRAME_PROFILE_CONFIRM) &&
        (type != BRIDGE_FRAME_SESSION_START) &&
        (type != BRIDGE_FRAME_HOP_SWITCH) &&
        (type != BRIDGE_FRAME_HOP_CONFIRM) &&
        (type != BRIDGE_FRAME_SWD_ABORT)) {
        return;
    }

    if ((config->device_mode == DEVICE_MODE_WIRELESS_SLAVE) &&
        s_pending &&
        serial_bridge_next_swd_request_confirms_response(
            (radio_frame_type_t)s_pending_frame[3], type)) {
        /* 下一请求只会在主机收到上一响应后产生。显式响应 ACK 丢失时，
         * 在分发新请求前释放旧响应槽，避免等待 120 ms 重试窗口。 */
        s_pending = false;
        s_waiting_ack = false;
        s_retries = 0U;
    }

    duplicate = radio_protocol_key_equal(&key, &s_last_rx_key);
    if (!duplicate) {
    if (type == BRIDGE_FRAME_DATA) {
            uint8_t data[BRIDGE_PAYLOAD_SIZE];
            uint8_t data_length;

            if (!radio_window_rx_accept(&s_data_rx_window, sequence,
                                         payload, length)) {
                /* 窗口外或重复 DATA 仍发送累计 ACK。 */
                duplicate = true;
            }
            while (radio_window_rx_take(&s_data_rx_window, data,
                                        &data_length)) {
                if (!serial_service_deliver_data(config->device_mode,
                                                 data, data_length)) {
                    return;
                }
            }
        } else if ((type == BRIDGE_FRAME_LINE_CODING) &&
                   (config->device_mode == DEVICE_MODE_WIRELESS_SLAVE) &&
                   (length == 7U)) {
            if (!serial_service_deliver_line_coding(payload, length)) {
                return;
            }
        } else if ((type == BRIDGE_FRAME_SWD_COMMAND) &&
                   (config->device_mode ==
                    DEVICE_MODE_WIRELESS_SLAVE)) {
            if (!swd_bridge_service_wireless_command(payload, length)) {
                return;
            }
        } else if ((type == RADIO_FRAME_SWD_BLOCK) &&
                   (config->device_mode ==
                    DEVICE_MODE_WIRELESS_SLAVE)) {
            if (!swd_bridge_service_wireless_block_request(payload,
                                                           length)) {
                return;
            }
        } else if ((type == RADIO_FRAME_SWD_BURST) &&
                   (config->device_mode ==
                    DEVICE_MODE_WIRELESS_SLAVE)) {
            if (!swd_bridge_service_wireless_burst_request(payload,
                                                           length)) {
                return;
            }
        } else if ((type == BRIDGE_FRAME_SWD_COMMAND_RESPONSE) &&
                   (config->device_mode ==
                    DEVICE_MODE_WIRELESS_HOST)) {
            if (!swd_bridge_service_wireless_response(payload,
                                                       length)) {
                return;
            }
            DAP_DIAG(swd_response());
            if (s_pending &&
                (s_pending_frame[3] == BRIDGE_FRAME_SWD_COMMAND)) {
                s_pending = false;
                s_waiting_ack = false;
                s_retries = 0U;
            }
        } else if ((type == RADIO_FRAME_SWD_BLOCK_RESPONSE) &&
                   (config->device_mode == DEVICE_MODE_WIRELESS_HOST)) {
            if (!swd_bridge_service_wireless_block_response(payload,
                                                            length)) {
                return;
            }
            DAP_DIAG(swd_response());
            if (s_pending &&
                (s_pending_frame[3] == RADIO_FRAME_SWD_BLOCK)) {
                s_pending = false;
                s_waiting_ack = false;
                s_retries = 0U;
            }
        } else if ((type == RADIO_FRAME_SWD_BURST_RESPONSE) &&
                   (config->device_mode ==
                    DEVICE_MODE_WIRELESS_HOST)) {
            if (!swd_bridge_service_wireless_burst_response(payload,
                                                            length)) {
                DAP_DIAG(burst_parse_error());
                return;
            }
            DAP_DIAG(burst_response_bytes(length));
            DAP_DIAG(swd_response());
            if (s_pending &&
                (s_pending_frame[3] == RADIO_FRAME_SWD_BURST)) {
                s_pending = false;
                s_waiting_ack = false;
                s_retries = 0U;
            }
        } else if ((type == BRIDGE_FRAME_SWD_ABORT) &&
                   (config->device_mode ==
                    DEVICE_MODE_WIRELESS_SLAVE) &&
                   (length == 1U)) {
            (void)swd_bridge_service_wireless_abort(payload[0]);
        } else if ((type == BRIDGE_FRAME_PROFILE_SWITCH) &&
                   (config->device_mode ==
                    DEVICE_MODE_WIRELESS_SLAVE) &&
                   (config->rate_mode == DEVICE_RATE_AUTO) &&
                   (length == 1U) &&
                   (payload[0] < SX128X_PROFILE_COUNT)) {
            s_profile_after_ack =
                (sx128x_profile_t)payload[0];
            s_switch_after_ack =
                s_profile_after_ack != sx128x_get_profile();
        } else if ((type == BRIDGE_FRAME_PROFILE_CONFIRM) &&
                   (config->device_mode ==
                    DEVICE_MODE_WIRELESS_SLAVE) &&
                   (length == 1U) &&
                   (payload[0] ==
                    (uint8_t)sx128x_get_profile())) {
            s_profile_trial = false;
        } else if ((type == BRIDGE_FRAME_HOP_SWITCH) &&
                   (config->device_mode ==
                    DEVICE_MODE_WIRELESS_SLAVE) &&
                   (length == 1U) &&
                   frequency_hopping_channel_valid(payload[0])) {
            s_channel_after_ack = payload[0];
            s_channel_switch_after_ack =
                s_channel_after_ack != s_current_channel;
        } else if ((type == BRIDGE_FRAME_HOP_CONFIRM) &&
                   (config->device_mode ==
                    DEVICE_MODE_WIRELESS_SLAVE) &&
                   (length == 1U) &&
                   (payload[0] == s_current_channel)) {
            s_channel_trial = false;
        } else {
            return;
        }
        s_last_rx_key = key;
        if ((type != BRIDGE_FRAME_PROFILE_SWITCH) &&
            (sx128x_get_profile() == s_profile_after_ack)) {
            s_profile_trial = false;
        }
        activity_signal();
    } else if (((type == BRIDGE_FRAME_SWD_COMMAND) ||
                (type == RADIO_FRAME_SWD_BLOCK) ||
                (type == RADIO_FRAME_SWD_BURST)) &&
               (config->device_mode == DEVICE_MODE_WIRELESS_SLAVE) &&
               !s_pending) {
        /* Re-send the cached result if the request ACK or reply was lost. */
        swd_bridge_service_repeat_request();
    } else if ((type == BRIDGE_FRAME_PROFILE_SWITCH) &&
               (config->device_mode ==
                DEVICE_MODE_WIRELESS_SLAVE) &&
               (config->rate_mode == DEVICE_RATE_AUTO) &&
               (length == 1U) &&
               (payload[0] < SX128X_PROFILE_COUNT)) {
        s_profile_after_ack =
            (sx128x_profile_t)payload[0];
        s_switch_after_ack =
            s_profile_after_ack != sx128x_get_profile();
    } else if ((type == BRIDGE_FRAME_HOP_SWITCH) &&
               (config->device_mode ==
                DEVICE_MODE_WIRELESS_SLAVE) &&
               (length == 1U) &&
               frequency_hopping_channel_valid(payload[0])) {
        s_channel_after_ack = payload[0];
        s_channel_switch_after_ack =
            s_channel_after_ack != s_current_channel;
    }
    valid_rx_mark();
    if (!serial_bridge_request_ack_required(type) ||
        !swd_request_ack_allowed(type)) {
        return;
    }
    if (!ack_send(sequence, rx_status,
                  serial_bridge_ack_compact(
                      type, config->rate_mode == DEVICE_RATE_AUTO))) {
        s_switch_after_ack = false;
        s_channel_switch_after_ack = false;
        radio_fail();
    }
}

static void swd_radio_abort_poll(void)
{
    uint16_t irq_status;
    uint8_t frame[BRIDGE_FRAME_SIZE];
    uint8_t length;
    uint8_t offset;
    sx128x_packet_status_t packet_status;
    radio_frame_view_t view;

    serial_service_process();
    if ((device_config_get()->device_mode !=
         DEVICE_MODE_WIRELESS_SLAVE) ||
        !s_radio_ready || (s_tx_kind != TX_NONE) ||
        !radio_hal_irq_active()) {
        return;
    }
    if (sx128x_get_irq_status(&irq_status) != SX128X_RESULT_OK) {
        radio_fail();
        return;
    }
    if ((irq_status & SX128X_IRQ_RX_DONE) == 0U) {
        return;
    }
    if ((irq_status & (SX128X_IRQ_CRC_ERROR |
                       SX128X_IRQ_SYNC_WORD_ERROR)) != 0U) {
        if ((sx128x_clear_irq_status(irq_status) !=
             SX128X_RESULT_OK) ||
            !radio_start_receive()) {
            radio_fail();
        }
        return;
    }
    if ((sx128x_get_rx_buffer_status(&length, &offset) !=
         SX128X_RESULT_OK) ||
        (length > sizeof(frame)) ||
        (sx128x_read_buffer(offset, frame, length) !=
          SX128X_RESULT_OK) ||
         !packet_status_read(&packet_status) ||
        (sx128x_clear_irq_status(irq_status) != SX128X_RESULT_OK)) {
        radio_fail();
        return;
    }
    if (radio_protocol_parse(frame, length,
                             device_config_get()->network_id, &view) &&
        (view.type == BRIDGE_FRAME_SWD_ABORT) &&
        (view.payload_length == 1U) &&
        remote_session_accept(view.session, false)) {
        (void)swd_bridge_service_wireless_abort(view.payload[0]);
        valid_rx_mark();
        activity_signal();
        if (!ack_send(view.sequence, &packet_status, false)) {
            radio_fail();
        }
        return;
    }
    if (!radio_start_receive()) {
        radio_fail();
    }
}

static void radio_irq_process(void)
{
    uint16_t irq_status;

    /* DIO1 为电平触发：先读取并清除所有 IRQ 原因，再启动下一次 RX/TX。 */
    if (!radio_hal_irq_active()) {
        return;
    }
    if (sx128x_get_irq_status(&irq_status) != SX128X_RESULT_OK) {
        radio_fail();
        return;
    }
    if ((irq_status & SX128X_IRQ_TX_DONE) != 0U) {
        tx_kind_t completed = s_tx_kind;

        if (sx128x_clear_irq_status(irq_status) != SX128X_RESULT_OK) {
            radio_fail();
            return;
        }
        s_tx_kind = TX_NONE;
        DAP_DIAG(rf_tx_done());
        if ((completed == TX_ACK) && s_channel_switch_after_ack) {
            s_channel_before_trial = s_current_channel;
            if (!radio_channel_set(s_channel_after_ack, true)) {
                radio_fail();
                return;
            }
            s_channel_switch_after_ack = false;
            s_channel_trial = true;
            s_channel_trial_deadline =
                board_millis() + BRIDGE_CHANNEL_TRIAL_MS;
            ++s_channel_switches;
        } else if ((completed == TX_ACK) && s_switch_after_ack) {
            s_profile_before_trial = sx128x_get_profile();
            if ((sx128x_set_profile(s_profile_after_ack) !=
                 SX128X_RESULT_OK) ||
                !radio_start_receive()) {
                radio_fail();
                return;
            }
            s_switch_after_ack = false;
            s_profile_trial = true;
            s_profile_trial_deadline =
                board_millis() + BRIDGE_PROFILE_TRIAL_MS;
            ++s_profile_switches;
        } else if (serial_bridge_resume_rx_after_tx(completed == TX_ACK)) {
            if (!radio_start_receive()) {
                radio_fail();
                return;
            }
            DAP_DIAG(rx_restored());
        }
        if ((completed == TX_RELIABLE) && s_pending) {
            s_waiting_ack = true;
            s_deadline = board_millis() +
                         serial_bridge_reliable_ack_wait_ms(
                             (radio_frame_type_t)s_pending_frame[3],
                             board_device_id_hash(), s_retries);
        }
        return;
    }
    if ((irq_status & SX128X_IRQ_RX_DONE) != 0U) {
        uint8_t frame[BRIDGE_FRAME_SIZE];
        uint8_t length;
        uint8_t offset;
        sx128x_packet_status_t packet_status;

        if ((irq_status & (SX128X_IRQ_CRC_ERROR |
                           SX128X_IRQ_SYNC_WORD_ERROR)) != 0U) {
            if ((sx128x_clear_irq_status(irq_status) !=
                 SX128X_RESULT_OK) ||
                !radio_start_receive()) {
                radio_fail();
            }
            return;
        }
        if ((sx128x_get_rx_buffer_status(&length, &offset) !=
             SX128X_RESULT_OK) ||
            (length > sizeof(frame)) ||
            (sx128x_read_buffer(offset, frame, length) != SX128X_RESULT_OK) ||
            !packet_status_read(&packet_status) ||
            (sx128x_clear_irq_status(irq_status) != SX128X_RESULT_OK)) {
            radio_fail();
            return;
        }
        if (frame_valid(frame, length)) {
            frame_deliver(frame, length, &packet_status);
            if (s_radio_ready && (s_tx_kind == TX_NONE) &&
                !radio_start_receive()) {
                radio_fail();
            }
        } else {
            ++s_invalid_radio_frames;
            if (!radio_start_receive()) {
                radio_fail();
            }
        }
        return;
    }
    if ((irq_status & SX128X_IRQ_RX_TX_TIMEOUT) != 0U) {
        ++s_radio_timeouts;
        if (sx128x_clear_irq_status(irq_status) != SX128X_RESULT_OK) {
            radio_fail();
        } else if (s_tx_kind != TX_NONE) {
            radio_fail();
        } else if (!radio_start_receive()) {
            radio_fail();
        }
        return;
    }
    if (sx128x_clear_irq_status(irq_status) != SX128X_RESULT_OK) {
        radio_fail();
    } else if (s_tx_kind != TX_NONE) {
        radio_fail();
    }
}

static void swd_tunnel_process_pending(void)
{
    uint8_t response[BRIDGE_PAYLOAD_SIZE];
    uint8_t response_length;

    /* SWD 所有者生成响应期间，保持无线发送互斥。 */
    if (s_tx_kind != TX_NONE) {
        return;
    }
    swd_bridge_service_process();
    if (s_pending) {
        return;
    }
    if (swd_bridge_service_reply_take(response, &response_length)) {
        reliable_queue(swd_bridge_service_reply_is_burst()
                           ? RADIO_FRAME_SWD_BURST_RESPONSE
                       : swd_bridge_service_reply_is_block()
                           ? RADIO_FRAME_SWD_BLOCK_RESPONSE
                           : BRIDGE_FRAME_SWD_COMMAND_RESPONSE,
                       response, response_length);
    }
}

static void wireless_source_process(void)
{
    const device_config_t *config = device_config_get();
    uint8_t data[BRIDGE_PAYLOAD_SIZE];
    radio_frame_type_t type;
    uint8_t length;

    if (s_pending || (s_tx_kind != TX_NONE)) {
        return;
    }
    if (s_swd_abort_pending &&
        (config->device_mode == DEVICE_MODE_WIRELESS_HOST)) {
        data[0] = s_swd_abort_transaction;
        reliable_queue(BRIDGE_FRAME_SWD_ABORT, data, 1U);
        s_swd_abort_pending = false;
        return;
    }
    if (s_session_announce_pending) {
        reliable_queue(BRIDGE_FRAME_SESSION_START, NULL, 0U);
        s_session_announce_pending = false;
        return;
    }
    if ((config->device_mode == DEVICE_MODE_WIRELESS_HOST) &&
        (config->rate_mode == DEVICE_RATE_AUTO)) {
        sx128x_profile_t recommendation =
            link_adaptation_recommend(&s_link_adaptation,
                                      board_millis());

        if (recommendation != sx128x_get_profile()) {
            data[0] = (uint8_t)recommendation;
            reliable_queue(BRIDGE_FRAME_PROFILE_SWITCH, data, 1U);
            return;
        }
    }
    /* 无线 DATA 槽位满时必须在读取 CDC/UART 前背压，保持源数据可重试。 */
    if (radio_window_tx_free(&s_data_tx_window) == 0U) {
        return;
    }
    if (serial_service_source_take(config->device_mode, &type, data,
                                   &length)) {
        if (type == BRIDGE_FRAME_DATA) {
            uint32_t sequence;

            (void)radio_window_tx_push(&s_data_tx_window, data, length,
                                       &sequence);
        } else {
            reliable_queue(type, data, length);
        }
    }
}

bool serial_bridge_init(void)
{
    device_config_init();
    memset(s_pending_frame, 0, sizeof(s_pending_frame));
    s_pending = false;
    s_waiting_ack = false;
    s_tx_kind = TX_NONE;
    s_next_sequence = board_device_id_hash();
    s_local_session = 0U;
    s_session_generation = 0U;
    memset(&s_last_rx_key, 0, sizeof(s_last_rx_key));
    swd_bridge_service_init();
    s_activity_until = 0U;
    s_radio_recoveries = 0U;
    s_radio_timeouts = 0U;
    s_invalid_radio_frames = 0U;
    s_peer_session_changes = 0U;
    s_remote_session = 0U;
    s_remote_session_valid = false;
    s_remote_metrics_valid = false;
    s_profile_switches = 0U;
    s_profile_trial = false;
    s_switch_after_ack = false;
    s_channel_switches = 0U;
    s_hop_generation = 0U;
    s_channel_trial = false;
    s_channel_switch_after_ack = false;
    s_hop_request_pending = false;
    s_swd_abort_pending = false;
    s_swd_abort_active = false;
    s_session_announce_pending = false;
    s_data_tx_sequence = 1U;
    radio_window_init(&s_data_tx_window, s_data_tx_sequence, 1U);
    radio_window_init(&s_data_rx_window, 1U, 1U);
    target_swd_poll_hook_set(swd_radio_abort_poll);
    s_error = !serial_service_init();
    s_radio_ready = false;
    if (!s_error &&
        (device_config_get()->device_mode != DEVICE_MODE_WIRED)) {
        /* 延后射频配置到主循环，确保 USB DAP 在启动期间持续得到服务。 */
        s_recover_at = board_millis();
    }
    return !s_error;
}

bool serial_bridge_apply_config(void)
{
    s_pending = false;
    s_waiting_ack = false;
    s_tx_kind = TX_NONE;
    swd_bridge_service_reset();
    s_local_session = 0U;
    memset(&s_last_rx_key, 0, sizeof(s_last_rx_key));
    s_remote_metrics_valid = false;
    s_profile_trial = false;
    s_switch_after_ack = false;
    s_channel_trial = false;
    s_channel_switch_after_ack = false;
    s_hop_request_pending = false;
    s_hop_success_count = 0U;
    s_swd_abort_pending = false;
    s_swd_abort_active = false;
    s_session_announce_pending = false;
    s_data_tx_sequence = 1U;
    radio_window_init(&s_data_tx_window, s_data_tx_sequence, 1U);
    radio_window_init(&s_data_rx_window, 1U, 1U);
    if (device_config_get()->device_mode == DEVICE_MODE_WIRED) {
        radio_hal_frontend_set(RADIO_FRONTEND_STANDBY);
        s_radio_ready = false;
        s_error = false;
        return true;
    }
    s_radio_ready = radio_configure(true);
    s_error = !s_radio_ready;
    return s_radio_ready;
}

void serial_bridge_process(void)
{
    const device_config_t *config = device_config_get();

    /* 轮询顺序固定：先排空本地串口输入，再完成 SWD，最后处理有线或无线
     * 传输状态。 */
    serial_service_process();
    if (config->device_mode == DEVICE_MODE_WIRED) {
        swd_tunnel_process_pending();
        if (serial_service_wired_process()) {
            activity_signal();
        }
        return;
    }
    if (!s_radio_ready) {
        if ((int32_t)(board_millis() - s_recover_at) >= 0) {
            s_radio_ready = radio_configure(s_local_session == 0U);
            s_error = !s_radio_ready;
            if (s_radio_ready) {
                ++s_radio_recoveries;
            }
            s_recover_at = board_millis() + BRIDGE_RECOVERY_DELAY_MS;
        }
        return;
    }

    radio_irq_process();
    if (!s_radio_ready) {
        return;
    }
    /* 先处理无线 IRQ 并在 TX_DONE 后恢复 RX，再推进从机 SWD。这样既允许
     * 短 block 在同一轮主循环完成，也为对端重新开启接收保留转向窗口。 */
    swd_tunnel_process_pending();
    if ((config->rate_mode == DEVICE_RATE_AUTO) &&
        serial_bridge_background_recovery_allowed(
            swd_bridge_service_busy()) &&
        (s_tx_kind == TX_NONE) &&
        (!s_pending ||
         ((s_pending_frame[3] != BRIDGE_FRAME_PROFILE_SWITCH) &&
          (s_pending_frame[3] != BRIDGE_FRAME_PROFILE_CONFIRM))) &&
        ((uint32_t)(board_millis() - s_last_valid_rx_ms) >=
         BRIDGE_RENDEZVOUS_MS) &&
        (sx128x_get_profile() != BRIDGE_RENDEZVOUS_PROFILE)) {
        if ((sx128x_set_profile(BRIDGE_RENDEZVOUS_PROFILE) !=
             SX128X_RESULT_OK) ||
            !radio_start_receive()) {
            radio_fail();
            return;
        }
        link_adaptation_profile_changed(
            &s_link_adaptation, BRIDGE_RENDEZVOUS_PROFILE,
            board_millis());
    }
    if (s_profile_trial && !s_pending &&
        ((int32_t)(board_millis() -
                   s_profile_trial_deadline) >= 0)) {
        if ((sx128x_set_profile(s_profile_before_trial) !=
             SX128X_RESULT_OK) ||
            !radio_start_receive()) {
            radio_fail();
            return;
        }
        s_profile_trial = false;
    }
    if (s_channel_trial && !s_pending &&
        ((int32_t)(board_millis() -
                   s_channel_trial_deadline) >= 0)) {
        if (!radio_channel_set(s_channel_before_trial, true)) {
            radio_fail();
            return;
        }
        s_channel_trial = false;
    }
    if (s_waiting_ack &&
        ((int32_t)(board_millis() - s_deadline) >= 0)) {
        bridge_frame_type_t pending_type =
            (bridge_frame_type_t)s_pending_frame[3];

        s_waiting_ack = false;
        if (config->rate_mode == DEVICE_RATE_AUTO) {
            link_adaptation_record_failure(&s_link_adaptation);
        }
        frequency_hopping_record_failure(
            &s_frequency_hopping, s_current_channel);
        if (++s_retries > BRIDGE_MAX_RETRIES) {
            if (pending_type == BRIDGE_FRAME_PROFILE_CONFIRM) {
                if ((sx128x_set_profile(s_profile_before_trial) !=
                     SX128X_RESULT_OK) ||
                    !radio_start_receive()) {
                    radio_fail();
                    return;
                }
                link_adaptation_profile_changed(
                    &s_link_adaptation, s_profile_before_trial,
                    board_millis());
                s_pending = false;
                s_retries = 0U;
                return;
            }
            if (pending_type == BRIDGE_FRAME_PROFILE_SWITCH) {
                link_adaptation_profile_changed(
                    &s_link_adaptation, sx128x_get_profile(),
                    board_millis());
                s_pending = false;
                s_retries = 0U;
                return;
            }
            if (pending_type == BRIDGE_FRAME_HOP_CONFIRM) {
                if (!radio_channel_set(s_channel_before_trial, true)) {
                    radio_fail();
                    return;
                }
                s_channel_trial = false;
                s_pending = false;
                s_retries = 0U;
                return;
            }
            if (pending_type == BRIDGE_FRAME_HOP_SWITCH) {
                s_pending = false;
                s_retries = 0U;
                return;
            }
            radio_fail();
            return;
        }
        if (frame_type_allows_retry_hop(pending_type)) {
            uint8_t retry_channel =
                recovery_channel_get(board_millis());

            if ((retry_channel != s_current_channel) &&
                !radio_channel_set(retry_channel, true)) {
                radio_fail();
                return;
            }
        }
    }
    if (s_pending && !s_waiting_ack && (s_tx_kind == TX_NONE)) {
        if (!frame_transmit(s_pending_frame, s_pending_length,
                            TX_RELIABLE)) {
            radio_fail();
            return;
        }
    }
    wireless_source_process();
    if (data_frame_transmit()) {
        return;
    }
    if (!s_pending && (s_tx_kind == TX_NONE) &&
        serial_bridge_background_recovery_allowed(
            swd_bridge_service_busy()) &&
        (radio_window_tx_active(&s_data_tx_window) == 0U) &&
        !s_profile_trial && !s_channel_trial &&
        ((int32_t)(board_millis() - s_channel_scan_at) >= 0)) {
        uint8_t scan_channel = serial_bridge_idle_recovery_channel(
            frequency_hopping_rendezvous(&s_frequency_hopping),
            recovery_channel_get(board_millis()));

        if ((scan_channel != s_current_channel) &&
            !radio_channel_set(scan_channel, true)) {
            radio_fail();
            return;
        }
        s_channel_scan_at =
            board_millis() + BRIDGE_CHANNEL_SCAN_DWELL_MS;
    }
}

bool serial_bridge_has_error(void)
{
    return s_error;
}

bool serial_bridge_activity_led(void)
{
    return (int32_t)(s_activity_until - board_millis()) > 0;
}

static bool swd_command_queue(const uint8_t *payload, uint8_t length)
{
    device_mode_t mode = device_config_get()->device_mode;

    /* SWD 桥一次只接受一个事务。无线 ACK 仍在发送时可以先进入可靠
     * 队列，serial_bridge_process() 会在 TX_DONE 后开始实际发送。 */
    if ((length == 0U) || s_pending) {
        return false;
    }
    if (!swd_bridge_service_begin(mode, payload, length)) {
        return false;
    }
    if (mode == DEVICE_MODE_WIRED) {
        return true;
    }
    reliable_queue(BRIDGE_FRAME_SWD_COMMAND, payload, length);
    return s_pending;
}

bool serial_bridge_swd_connect(uint8_t transaction_id)
{
    uint8_t payload[2];

    return swd_command_queue(payload,
                             swd_tunnel_encode_connect(transaction_id,
                                                       payload));
}

bool serial_bridge_swd_disconnect(uint8_t transaction_id)
{
    uint8_t payload[2];

    return swd_command_queue(
        payload, swd_tunnel_encode_disconnect(transaction_id, payload));
}

bool serial_bridge_swd_reset(uint8_t transaction_id)
{
    uint8_t payload[2];

    return swd_command_queue(payload,
                             swd_tunnel_encode_reset(transaction_id,
                                                     payload));
}

bool serial_bridge_swd_sequence(uint8_t transaction_id,
                                uint16_t bit_count,
                                const uint8_t *data)
{
    uint8_t payload[BRIDGE_PAYLOAD_SIZE];

    return swd_command_queue(
        payload, swd_tunnel_encode_sequence(transaction_id, bit_count,
                                            data, payload));
}

bool serial_bridge_swd_sequence_io(uint8_t transaction_id,
                                   const uint8_t *request,
                                   uint8_t request_length)
{
    uint8_t payload[BRIDGE_PAYLOAD_SIZE];

    return swd_command_queue(
        payload, swd_tunnel_encode_swd_sequence(
                     transaction_id, request, request_length, payload));
}

bool serial_bridge_swd_clock(uint8_t transaction_id, uint32_t clock_hz)
{
    uint8_t payload[6];

    return swd_command_queue(
        payload, swd_tunnel_encode_clock(transaction_id, clock_hz,
                                         payload));
}

bool serial_bridge_swd_configure(uint8_t transaction_id,
                                 uint8_t idle_cycles,
                                 uint16_t retry_count,
                                 uint16_t match_retry,
                                 uint8_t turnaround,
                                 bool data_phase)
{
    uint8_t payload[9];

    return swd_command_queue(
        payload, swd_tunnel_encode_configure(
                     transaction_id, idle_cycles, retry_count,
                     match_retry, turnaround, data_phase, payload));
}

bool serial_bridge_swd_pins(uint8_t transaction_id, uint8_t value,
                            uint8_t select, uint32_t wait_us)
{
    uint8_t payload[8];

    return swd_command_queue(
        payload, swd_tunnel_encode_pins(transaction_id, value, select,
                                        wait_us, payload));
}

bool serial_bridge_swd_transfers(
    uint8_t transaction_id, const swd_tunnel_transfer_t *transfers,
    uint8_t count)
{
    DAP_DIAG(swd_queued(count));
    DAP_DIAG(single_swd());
    uint8_t payload[BRIDGE_PAYLOAD_SIZE];
    device_mode_t mode = device_config_get()->device_mode;

    if ((transfers == NULL) || (count == 0U) || s_pending) {
        return false;
    }
    if ((mode == DEVICE_MODE_WIRED) ||
        (mode == DEVICE_MODE_WIRELESS_HOST)) {
        if (!swd_bridge_service_begin_block(mode, transaction_id, transfers,
                                            count)) {
            return false;
        }
    }
    if (mode == DEVICE_MODE_WIRELESS_HOST) {
        uint8_t length = swd_tunnel_encode_block(transaction_id, transfers,
                                                 count, payload);

        if (length == 0U) {
            (void)swd_bridge_service_cancel(transaction_id);
            return false;
        }
        reliable_queue(RADIO_FRAME_SWD_BLOCK, payload, length);
        return s_pending;
    }
    return mode == DEVICE_MODE_WIRED;
}

bool serial_bridge_swd_response_take(swd_tunnel_response_t *response)
{
    return swd_bridge_service_response_take(response);
}

bool serial_bridge_swd_burst(const swd_tunnel_burst_t *burst)
{
    uint8_t payload[BRIDGE_PAYLOAD_SIZE];
    uint8_t length;

    if ((burst == NULL) || s_pending ||
        (device_config_get()->device_mode != DEVICE_MODE_WIRELESS_HOST)) {
        return false;
    }
    length = swd_tunnel_burst_encode(burst, payload);
    if ((length == 0U) ||
        !swd_bridge_service_begin_burst(DEVICE_MODE_WIRELESS_HOST,
                                        burst)) {
        return false;
    }
    reliable_queue(RADIO_FRAME_SWD_BURST, payload, length);
    if (!s_pending) {
        (void)swd_bridge_service_cancel(burst->transaction_id);
        return false;
    }
    DAP_DIAG(burst_queued(burst->count, length));
    return true;
}

bool serial_bridge_swd_burst_response_take(
    swd_tunnel_burst_response_t *response)
{
    return swd_bridge_service_burst_response_take(response);
}

void serial_bridge_swd_pump(void)
{
    /* 只推进本地 SWD 引擎和它的响应路由。无线模式下响应来自射频，隧道空闲，
     * 因此这里不会重复消费射频状态。 */
    swd_tunnel_process_pending();
}

void serial_bridge_swd_cancel(uint8_t transaction_id)
{
    device_mode_t mode = device_config_get()->device_mode;

    if (!swd_bridge_service_cancel(transaction_id)) {
        return;
    }
    if (s_pending &&
        ((s_pending_frame[3] == BRIDGE_FRAME_SWD_COMMAND) ||
         (s_pending_frame[3] == RADIO_FRAME_SWD_BLOCK) ||
         (s_pending_frame[3] == RADIO_FRAME_SWD_BURST)) &&
        (s_pending_frame[BRIDGE_HEADER_SIZE + 1U] ==
         transaction_id)) {
        s_pending = false;
        s_waiting_ack = false;
        s_retries = 0U;
    }
    if (mode == DEVICE_MODE_WIRELESS_HOST) {
        s_swd_abort_transaction = transaction_id;
        s_swd_abort_pending = true;
        s_swd_abort_active = true;
    }
}

bool serial_bridge_swd_cancel_complete(uint8_t transaction_id)
{
    return !s_swd_abort_active ||
           (transaction_id != s_swd_abort_transaction);
}

void serial_bridge_status_get(serial_bridge_status_t *status)
{
    if (status == NULL) {
        return;
    }
    status->radio_recoveries = s_radio_recoveries;
    status->swd_cancellations =
        swd_bridge_service_cancellations();
    status->stale_swd_responses =
        swd_bridge_service_stale_responses();
    status->uart_rx_overruns = serial_service_rx_overruns();
    status->radio_timeouts = s_radio_timeouts;
    status->invalid_radio_frames = s_invalid_radio_frames;
    status->peer_session_changes = s_peer_session_changes;
    status->remote_rssi_dbm_x2 =
        s_last_remote_metrics.rssi_dbm_x2;
    status->device_mode = (uint8_t)device_config_get()->device_mode;
    status->retries = s_retries;
    status->radio_profile = (uint8_t)sx128x_get_profile();
    status->profile_switches = s_profile_switches;
    status->radio_channel = s_current_channel;
    status->channel_switches = s_channel_switches;
    status->remote_error_status =
        s_last_remote_metrics.error_status;
    status->remote_tx_rx_status =
        s_last_remote_metrics.tx_rx_status;
    status->remote_sync_status =
        s_last_remote_metrics.sync_address_status;
    status->remote_metrics_valid = s_remote_metrics_valid;
    status->radio_ready = s_radio_ready;
    status->error = s_error;
    status->swd_request_active =
        swd_bridge_service_request_active();
}
