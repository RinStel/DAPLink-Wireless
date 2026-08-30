#include "dap_diagnostics.h"

#if CMSIS_DAP_DIAGNOSTICS_ENABLE

#include <string.h>

#include "board.h"

#define DAP_TRANSFER       0x05U
#define DAP_TRANSFER_BLOCK 0x06U
#define DAP_QUEUE_COMMANDS 0x7EU
#define DAP_EXECUTE_COMMANDS 0x7FU
#define DIAGNOSTICS_VERSION 2U

typedef struct {
    uint32_t session_start;
    uint32_t usb_out_count;
    uint32_t transfer_count;
    uint32_t transfer_block_count;
    uint32_t queue_count;
    uint32_t execute_count;
    uint32_t transfer_items;
    uint32_t histogram[6];
    uint32_t rf_tx_frames;
    uint32_t rf_tx_bytes;
    uint32_t rf_retransmits;
    uint32_t ack_cycles;
    uint32_t ack_count;
    uint32_t ack_max_cycles;
    uint32_t swd_cycles;
    uint32_t swd_count;
    uint32_t swd_max_cycles;
    uint32_t usb_in_cycles;
    uint32_t usb_in_count;
    uint32_t usb_in_max_cycles;
    uint32_t rx_restore_cycles;
    uint32_t rx_restore_count;
    uint32_t rx_restore_max_cycles;
    uint32_t request_depth_sum;
    uint32_t request_depth_samples;
    uint32_t request_depth_max;
    uint32_t tx_done_at;
    uint32_t request_ack_at;
    uint32_t response_at;
    uint32_t burst_tx_count;
    uint32_t burst_command_total;
    uint32_t burst_max_commands;
    uint32_t burst_histogram[3];
    uint32_t single_swd_tx_count;
    uint32_t burst_request_bytes;
    uint32_t burst_response_bytes;
    uint32_t burst_fallback_count;
    uint32_t burst_parse_error_count;
    uint32_t burst_reject_parse_count;
    uint32_t burst_reject_capacity_count;
    uint32_t burst_reject_bridge_count;
} dap_diagnostics_t;

static dap_diagnostics_t s_stats;

static void add_sample(uint32_t sample, uint32_t *sum, uint32_t *count,
                       uint32_t *maximum)
{
    if (UINT32_MAX - *sum < sample) *sum = UINT32_MAX;
    else *sum += sample;
    if (*count != UINT32_MAX) ++*count;
    if (sample > *maximum) *maximum = sample;
}

static void put_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value; output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16); output[3] = (uint8_t)(value >> 24);
}

static void add_saturated(uint32_t *value, uint32_t amount)
{
    if (UINT32_MAX - *value < amount) *value = UINT32_MAX;
    else *value += amount;
}

void dap_diagnostics_reset(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.session_start = board_cycle_count();
}

void dap_diagnostics_usb_out(const uint8_t *request, uint8_t length)
{
    uint16_t count = 0U;
    uint8_t bucket;
    if ((request == NULL) || (length == 0U)) return;
    if (s_stats.session_start == 0U) s_stats.session_start = board_cycle_count();
    if (s_stats.usb_out_count != UINT32_MAX) ++s_stats.usb_out_count;
    if (request[0] == DAP_TRANSFER && length >= 3U) {
        ++s_stats.transfer_count; count = request[2];
    } else if (request[0] == DAP_TRANSFER_BLOCK && length >= 5U) {
        ++s_stats.transfer_block_count;
        count = (uint16_t)request[2] | ((uint16_t)request[3] << 8);
    } else if (request[0] == DAP_QUEUE_COMMANDS) ++s_stats.queue_count;
    else if (request[0] == DAP_EXECUTE_COMMANDS) ++s_stats.execute_count;
    if (count == 0U) return;
    s_stats.transfer_items += count;
    bucket = count == 1U ? 0U : count == 2U ? 1U : count <= 4U ? 2U :
             count <= 8U ? 3U : count <= 12U ? 4U : 5U;
    ++s_stats.histogram[bucket];
}

void dap_diagnostics_swd_queued(uint8_t transfer_count)
{
    (void)transfer_count;
}

void dap_diagnostics_rf_tx_start(uint8_t frame_length, bool retransmit)
{
    ++s_stats.rf_tx_frames; s_stats.rf_tx_bytes += frame_length;
    if (retransmit) ++s_stats.rf_retransmits;
}

void dap_diagnostics_rf_tx_done(void) { s_stats.tx_done_at = board_cycle_count(); }

void dap_diagnostics_rx_restored(void)
{
    add_sample(board_cycle_count() - s_stats.tx_done_at,
               &s_stats.rx_restore_cycles, &s_stats.rx_restore_count,
               &s_stats.rx_restore_max_cycles);
}

void dap_diagnostics_request_ack(void)
{
    uint32_t now = board_cycle_count();
    add_sample(now - s_stats.tx_done_at, &s_stats.ack_cycles,
               &s_stats.ack_count, &s_stats.ack_max_cycles);
    s_stats.request_ack_at = now;
}

void dap_diagnostics_swd_response(void)
{
    uint32_t now = board_cycle_count();
    add_sample(now - s_stats.request_ack_at, &s_stats.swd_cycles,
               &s_stats.swd_count, &s_stats.swd_max_cycles);
    s_stats.response_at = now;
}

void dap_diagnostics_usb_in_complete(void)
{
    if (s_stats.response_at != 0U) {
        add_sample(board_cycle_count() - s_stats.response_at,
                   &s_stats.usb_in_cycles, &s_stats.usb_in_count,
                   &s_stats.usb_in_max_cycles);
        s_stats.response_at = 0U;
    }
}

void dap_diagnostics_request_ring_depth(uint8_t depth)
{
    if (UINT32_MAX - s_stats.request_depth_sum < depth)
        s_stats.request_depth_sum = UINT32_MAX;
    else s_stats.request_depth_sum += depth;
    if (s_stats.request_depth_samples != UINT32_MAX)
        ++s_stats.request_depth_samples;
    if (depth > s_stats.request_depth_max) s_stats.request_depth_max = depth;
}

void dap_diagnostics_burst_queued(uint8_t count, uint8_t request_bytes)
{
    if ((count < 2U) || (count > 3U)) return;
    add_saturated(&s_stats.burst_tx_count, 1U);
    add_saturated(&s_stats.burst_command_total, count);
    if (count > s_stats.burst_max_commands)
        s_stats.burst_max_commands = count;
    add_saturated(&s_stats.burst_histogram[count - 1U], 1U);
    add_saturated(&s_stats.burst_request_bytes, request_bytes);
}

void dap_diagnostics_burst_response_bytes(uint8_t response_bytes)
{
    add_saturated(&s_stats.burst_response_bytes, response_bytes);
}

void dap_diagnostics_single_swd(void)
{
    add_saturated(&s_stats.single_swd_tx_count, 1U);
}

void dap_diagnostics_burst_fallback(void)
{
    add_saturated(&s_stats.burst_fallback_count, 1U);
    add_saturated(&s_stats.burst_histogram[0], 1U);
}

void dap_diagnostics_burst_parse_error(void)
{
    add_saturated(&s_stats.burst_parse_error_count, 1U);
}

void dap_diagnostics_burst_reject_parse(void)
{
    add_saturated(&s_stats.burst_reject_parse_count, 1U);
}

void dap_diagnostics_burst_reject_capacity(void)
{
    add_saturated(&s_stats.burst_reject_capacity_count, 1U);
}

void dap_diagnostics_burst_reject_bridge(void)
{
    add_saturated(&s_stats.burst_reject_bridge_count, 1U);
}

uint8_t dap_diagnostics_page(uint8_t page, uint8_t *output, uint8_t capacity)
{
    uint32_t values[15] = {0U};
    uint8_t i;
    if ((output == NULL) || (capacity < 60U) || (page > 3U)) return 0U;
    if (page == 0U) {
        values[0] = DIAGNOSTICS_VERSION;
        values[1] = board_cycles_from_us(1U);
        values[2] = board_cycle_count() - s_stats.session_start;
        values[3] = s_stats.usb_out_count; values[4] = s_stats.transfer_count;
        values[5] = s_stats.transfer_block_count; values[6] = s_stats.queue_count;
        values[7] = s_stats.execute_count; values[8] = s_stats.transfer_items;
        memcpy(&values[9], s_stats.histogram, sizeof(s_stats.histogram));
    } else if (page == 1U) {
        values[0] = s_stats.rf_tx_frames; values[1] = s_stats.rf_tx_bytes;
        values[2] = s_stats.rf_retransmits; values[3] = s_stats.ack_cycles;
        values[4] = s_stats.ack_count; values[5] = s_stats.ack_max_cycles;
        values[6] = s_stats.swd_cycles; values[7] = s_stats.swd_count;
        values[8] = s_stats.swd_max_cycles; values[9] = s_stats.usb_in_cycles;
        values[10] = s_stats.usb_in_count; values[11] = s_stats.usb_in_max_cycles;
    } else if (page == 2U) {
        values[0] = s_stats.rx_restore_cycles;
        values[1] = s_stats.rx_restore_count;
        values[2] = s_stats.rx_restore_max_cycles;
        values[3] = s_stats.request_depth_max;
        values[4] = s_stats.request_depth_sum;
        values[5] = s_stats.request_depth_samples;
    } else {
        values[0] = s_stats.burst_tx_count;
        values[1] = s_stats.burst_command_total;
        values[2] = s_stats.burst_max_commands;
        memcpy(&values[3], s_stats.burst_histogram,
               sizeof(s_stats.burst_histogram));
        values[6] = s_stats.single_swd_tx_count;
        values[7] = s_stats.burst_request_bytes;
        values[8] = s_stats.burst_response_bytes;
        values[9] = s_stats.burst_fallback_count;
        values[10] = s_stats.burst_parse_error_count;
        values[11] = s_stats.burst_reject_parse_count;
        values[12] = s_stats.burst_reject_capacity_count;
        values[13] = s_stats.burst_reject_bridge_count;
    }
    for (i = 0U; i < 15U; ++i) put_u32(&output[i * 4U], values[i]);
    return 60U;
}

#endif
