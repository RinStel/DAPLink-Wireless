#include "serial_bridge.h"

#include <string.h>

#include "board.h"
#include "device_config.h"
#include "frequency_hopping.h"
#include "link_adaptation.h"
#include "radio_hal.h"
#include "radio_protocol.h"
#include "serial_service.h"
#include "sx128x.h"
#include "swd_bridge_service.h"
#include "swd_tunnel.h"

#define BRIDGE_HEADER_SIZE          RADIO_PROTOCOL_HEADER_SIZE
#define BRIDGE_PAYLOAD_SIZE         RADIO_PROTOCOL_PAYLOAD_SIZE
#define BRIDGE_FRAME_SIZE           RADIO_PROTOCOL_FRAME_SIZE
#define BRIDGE_ACK_TIMEOUT_MS       120U
#define BRIDGE_SWD_ACK_TIMEOUT_MS   3500U
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
#define BRIDGE_HOP_INTERVAL          8U
#define BRIDGE_ACK_METRICS_SIZE      7U
#define BRIDGE_RENDEZVOUS_PROFILE   SX128X_PROFILE_GFSK_1M

#define BRIDGE_FRAME_DATA            RADIO_FRAME_DATA
#define BRIDGE_FRAME_ACK             RADIO_FRAME_ACK
#define BRIDGE_FRAME_LINE_CODING     RADIO_FRAME_LINE_CODING
#define BRIDGE_FRAME_SWD_REQUEST     RADIO_FRAME_SWD_REQUEST
#define BRIDGE_FRAME_SWD_RESPONSE    RADIO_FRAME_SWD_RESPONSE
#define BRIDGE_FRAME_PROFILE_SWITCH  RADIO_FRAME_PROFILE_SWITCH
#define BRIDGE_FRAME_PROFILE_CONFIRM RADIO_FRAME_PROFILE_CONFIRM
#define BRIDGE_FRAME_SESSION_START   RADIO_FRAME_SESSION_START
#define BRIDGE_FRAME_HOP_SWITCH      RADIO_FRAME_HOP_SWITCH
#define BRIDGE_FRAME_HOP_CONFIRM     RADIO_FRAME_HOP_CONFIRM

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
    if ((s_tx_kind != TX_NONE) ||
        (sx128x_standby() != SX128X_RESULT_OK) ||
        (sx128x_start_tx(frame, length) != SX128X_RESULT_OK)) {
        return false;
    }
    s_tx_kind = kind;
    activity_signal();
    return true;
}

static void reliable_queue(bridge_frame_type_t type,
                           const uint8_t *payload, uint8_t length)
{
    if (s_pending || (length > BRIDGE_PAYLOAD_SIZE)) {
        return;
    }
    s_pending_sequence = ++s_next_sequence;
    s_pending_length = frame_build(s_pending_frame, type,
                                   s_pending_sequence, payload, length);
    s_retries = 0U;
    s_pending = true;
    s_waiting_ack = false;
}

static bool ack_send(uint32_t sequence,
                     const sx128x_packet_status_t *status)
{
    uint8_t frame[BRIDGE_HEADER_SIZE + BRIDGE_ACK_METRICS_SIZE];
    uint8_t payload[BRIDGE_ACK_METRICS_SIZE];
    uint8_t length;

    payload[0] = (uint8_t)((uint16_t)status->rssi_dbm_x2 >> 8);
    payload[1] = (uint8_t)status->rssi_dbm_x2;
    payload[2] = status->error_status;
    payload[3] = status->tx_rx_status;
    payload[4] = status->sync_address_status;
    payload[5] = (uint8_t)sx128x_get_profile();
    payload[6] = s_current_channel;
    length = frame_build(frame, BRIDGE_FRAME_ACK, sequence, payload,
                         sizeof(payload));

    return frame_transmit(frame, length, TX_ACK);
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
           (type == BRIDGE_FRAME_SWD_REQUEST) ||
           (type == BRIDGE_FRAME_SWD_RESPONSE);
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

static void frame_deliver(const uint8_t *frame,
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

    if (!radio_protocol_parse(frame, frame[16] + BRIDGE_HEADER_SIZE,
                              config->network_id, &view)) {
        return;
    }
    radio_protocol_key_get(&view, &key);
    type = view.type;
    sequence = view.sequence;
    payload = view.payload;
    length = view.payload_length;
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
        if (!ack_send(sequence, rx_status)) {
            radio_fail();
        }
        return;
    }
    if (type == BRIDGE_FRAME_ACK) {
        if (s_pending && (sequence == s_pending_sequence)) {
            bridge_frame_type_t pending_type =
                (bridge_frame_type_t)s_pending_frame[3];

            if ((length != BRIDGE_ACK_METRICS_SIZE) ||
                (frame[BRIDGE_HEADER_SIZE + 5U] >=
                 SX128X_PROFILE_COUNT) ||
                !frequency_hopping_channel_valid(
                    frame[BRIDGE_HEADER_SIZE + 6U]) ||
                (frame[BRIDGE_HEADER_SIZE + 6U] !=
                 s_current_channel)) {
                return;
            }
            if (!remote_session_accept(view.session, false)) {
                return;
            }
            valid_rx_mark();
            s_last_remote_metrics.rssi_dbm_x2 =
                (int16_t)((uint16_t)
                              frame[BRIDGE_HEADER_SIZE] << 8 |
                          frame[BRIDGE_HEADER_SIZE + 1U]);
            s_last_remote_metrics.error_status =
                frame[BRIDGE_HEADER_SIZE + 2U];
            s_last_remote_metrics.tx_rx_status =
                frame[BRIDGE_HEADER_SIZE + 3U];
            s_last_remote_metrics.sync_address_status =
                frame[BRIDGE_HEADER_SIZE + 4U];
            s_remote_metrics_valid = true;
            frequency_hopping_record_success(
                &s_frequency_hopping, s_current_channel);
            if (device_config_get()->rate_mode == DEVICE_RATE_AUTO) {
                link_adaptation_record_success(
                    &s_link_adaptation,
                    s_last_remote_metrics.rssi_dbm_x2);
            }
            s_pending = false;
            s_waiting_ack = false;
            s_retries = 0U;
            if (frame_type_is_business(pending_type) &&
                (device_config_get()->device_mode ==
                 DEVICE_MODE_WIRELESS_HOST)) {
                if (++s_hop_success_count >= BRIDGE_HOP_INTERVAL) {
                    s_hop_success_count = 0U;
                    s_hop_request_pending = true;
                }
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
            }
        }
        return;
    }
    if (!remote_session_accept(view.session, false)) {
        return;
    }
    if ((type != BRIDGE_FRAME_DATA) &&
        (type != BRIDGE_FRAME_LINE_CODING) &&
        (type != BRIDGE_FRAME_SWD_REQUEST) &&
        (type != BRIDGE_FRAME_SWD_RESPONSE) &&
        (type != BRIDGE_FRAME_PROFILE_SWITCH) &&
        (type != BRIDGE_FRAME_PROFILE_CONFIRM) &&
        (type != BRIDGE_FRAME_SESSION_START) &&
        (type != BRIDGE_FRAME_HOP_SWITCH) &&
        (type != BRIDGE_FRAME_HOP_CONFIRM)) {
        return;
    }

    duplicate = radio_protocol_key_equal(&key, &s_last_rx_key);
    if (!duplicate) {
        if ((type == BRIDGE_FRAME_DATA) &&
            (config->device_mode == DEVICE_MODE_WIRELESS_HOST)) {
            if (!serial_service_deliver_data(config->device_mode,
                                             payload, length)) {
                return;
            }
        } else if ((type == BRIDGE_FRAME_DATA) &&
                   (config->device_mode == DEVICE_MODE_WIRELESS_SLAVE)) {
            if (!serial_service_deliver_data(config->device_mode,
                                             payload, length)) {
                return;
            }
        } else if ((type == BRIDGE_FRAME_LINE_CODING) &&
                   (config->device_mode == DEVICE_MODE_WIRELESS_SLAVE) &&
                   (length == 7U)) {
            if (!serial_service_deliver_line_coding(payload, length)) {
                return;
            }
        } else if ((type == BRIDGE_FRAME_SWD_REQUEST) &&
                   (config->device_mode ==
                    DEVICE_MODE_WIRELESS_SLAVE)) {
            if (!swd_bridge_service_wireless_request(payload, length)) {
                return;
            }
        } else if ((type == BRIDGE_FRAME_SWD_RESPONSE) &&
                   (config->device_mode ==
                    DEVICE_MODE_WIRELESS_HOST)) {
            if (!swd_bridge_service_wireless_response(payload,
                                                       length)) {
                return;
            }
            if (s_pending &&
                (s_pending_frame[3] == BRIDGE_FRAME_SWD_REQUEST)) {
                s_pending = false;
                s_waiting_ack = false;
                s_retries = 0U;
            }
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
    } else if ((type == BRIDGE_FRAME_SWD_REQUEST) &&
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
    if (!ack_send(sequence, rx_status)) {
        s_switch_after_ack = false;
        s_channel_switch_after_ack = false;
        radio_fail();
    }
}

static void radio_irq_process(void)
{
    uint16_t irq_status;

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
        } else {
            if (!radio_start_receive()) {
                radio_fail();
                return;
            }
        }
        if ((completed == TX_RELIABLE) && s_pending) {
            s_waiting_ack = true;
            s_deadline = board_millis() +
                         (s_pending_frame[3] ==
                                  BRIDGE_FRAME_SWD_REQUEST
                              ? BRIDGE_SWD_ACK_TIMEOUT_MS
                              : BRIDGE_ACK_TIMEOUT_MS) +
                         ((board_device_id_hash() +
                           (uint32_t)s_retries * 17U) % 41U);
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
            (sx128x_get_packet_status(&packet_status) !=
             SX128X_RESULT_OK) ||
            (sx128x_clear_irq_status(irq_status) != SX128X_RESULT_OK)) {
            radio_fail();
            return;
        }
        if (frame_valid(frame, length)) {
            frame_deliver(frame, &packet_status);
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

    swd_bridge_service_process();
    if (s_pending || (s_tx_kind != TX_NONE)) {
        return;
    }
    if (swd_bridge_service_reply_take(response, &response_length)) {
        reliable_queue(BRIDGE_FRAME_SWD_RESPONSE, response,
                       response_length);
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
    if (s_session_announce_pending) {
        reliable_queue(BRIDGE_FRAME_SESSION_START, NULL, 0U);
        s_session_announce_pending = false;
        return;
    }
    if ((config->device_mode == DEVICE_MODE_WIRELESS_HOST) &&
        s_hop_request_pending && !s_profile_trial &&
        !s_channel_trial) {
        data[0] = frequency_hopping_select(
            &s_frequency_hopping, ++s_hop_generation, 0U,
            s_current_channel);
        reliable_queue(BRIDGE_FRAME_HOP_SWITCH, data, 1U);
        s_hop_request_pending = false;
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
    if (serial_service_source_take(config->device_mode, &type, data,
                                   &length)) {
        reliable_queue(type, data, length);
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
    s_session_announce_pending = false;
    s_error = !serial_service_init();
    s_radio_ready = false;
    if (!s_error &&
        (device_config_get()->device_mode != DEVICE_MODE_WIRED)) {
        s_radio_ready = radio_configure(true);
        s_error = !s_radio_ready;
        if (s_error) {
            s_recover_at = board_millis() + BRIDGE_RECOVERY_DELAY_MS;
        }
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
    s_session_announce_pending = false;
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

    serial_service_process();
    swd_tunnel_process_pending();
    if (config->device_mode == DEVICE_MODE_WIRED) {
        if (serial_service_wired_process()) {
            activity_signal();
        }
        return;
    }
    if (!s_radio_ready) {
        if ((int32_t)(board_millis() - s_recover_at) >= 0) {
            s_radio_ready = radio_configure(false);
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
    if ((config->rate_mode == DEVICE_RATE_AUTO) &&
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
    if (!s_pending && (s_tx_kind == TX_NONE) &&
        !s_profile_trial && !s_channel_trial &&
        ((int32_t)(board_millis() - s_channel_scan_at) >= 0)) {
        uint8_t scan_channel =
            recovery_channel_get(board_millis());

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

static bool swd_request_queue(const uint8_t *payload, uint8_t length)
{
    device_mode_t mode = device_config_get()->device_mode;

    if ((length == 0U) || s_pending || (s_tx_kind != TX_NONE)) {
        return false;
    }
    if (!swd_bridge_service_begin(mode, payload, length)) {
        return false;
    }
    if (mode == DEVICE_MODE_WIRED) {
        return true;
    }
    reliable_queue(BRIDGE_FRAME_SWD_REQUEST, payload, length);
    return s_pending;
}

bool serial_bridge_swd_connect(uint8_t transaction_id)
{
    uint8_t payload[2];

    return swd_request_queue(payload,
                             swd_tunnel_encode_connect(transaction_id,
                                                       payload));
}

bool serial_bridge_swd_disconnect(uint8_t transaction_id)
{
    uint8_t payload[2];

    return swd_request_queue(
        payload, swd_tunnel_encode_disconnect(transaction_id, payload));
}

bool serial_bridge_swd_reset(uint8_t transaction_id)
{
    uint8_t payload[2];

    return swd_request_queue(payload,
                             swd_tunnel_encode_reset(transaction_id,
                                                     payload));
}

bool serial_bridge_swd_sequence(uint8_t transaction_id,
                                uint16_t bit_count,
                                const uint8_t *data)
{
    uint8_t payload[BRIDGE_PAYLOAD_SIZE];

    return swd_request_queue(
        payload, swd_tunnel_encode_sequence(transaction_id, bit_count,
                                            data, payload));
}

bool serial_bridge_swd_sequence_io(uint8_t transaction_id,
                                   const uint8_t *request,
                                   uint8_t request_length)
{
    uint8_t payload[BRIDGE_PAYLOAD_SIZE];

    return swd_request_queue(
        payload, swd_tunnel_encode_swd_sequence(
                     transaction_id, request, request_length, payload));
}

bool serial_bridge_swd_clock(uint8_t transaction_id, uint32_t clock_hz)
{
    uint8_t payload[6];

    return swd_request_queue(
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

    return swd_request_queue(
        payload, swd_tunnel_encode_configure(
                     transaction_id, idle_cycles, retry_count,
                     match_retry, turnaround, data_phase, payload));
}

bool serial_bridge_swd_pins(uint8_t transaction_id, uint8_t value,
                            uint8_t select, uint32_t wait_us)
{
    uint8_t payload[8];

    return swd_request_queue(
        payload, swd_tunnel_encode_pins(transaction_id, value, select,
                                        wait_us, payload));
}

bool serial_bridge_swd_transfers(
    uint8_t transaction_id, const swd_tunnel_transfer_t *transfers,
    uint8_t count)
{
    uint8_t payload[BRIDGE_PAYLOAD_SIZE];

    return swd_request_queue(
        payload, swd_tunnel_encode_transfers(transaction_id, transfers,
                                             count, payload));
}

bool serial_bridge_swd_response_take(swd_tunnel_response_t *response)
{
    return swd_bridge_service_response_take(response);
}

void serial_bridge_swd_cancel(uint8_t transaction_id)
{
    if (!swd_bridge_service_cancel(transaction_id)) {
        return;
    }
    if (s_pending &&
        (s_pending_frame[3] == BRIDGE_FRAME_SWD_REQUEST) &&
        (s_pending_frame[BRIDGE_HEADER_SIZE + 1U] ==
         transaction_id)) {
        s_pending = false;
        s_waiting_ack = false;
        s_retries = 0U;
    }
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
