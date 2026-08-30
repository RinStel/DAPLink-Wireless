#ifndef DAP_DIAGNOSTICS_H
#define DAP_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

#ifndef CMSIS_DAP_DIAGNOSTICS_ENABLE
#define CMSIS_DAP_DIAGNOSTICS_ENABLE 0
#endif

#if CMSIS_DAP_DIAGNOSTICS_ENABLE
void dap_diagnostics_reset(void);
uint8_t dap_diagnostics_page(uint8_t page, uint8_t *output,
                             uint8_t capacity);
void dap_diagnostics_usb_out(const uint8_t *request, uint8_t length);
void dap_diagnostics_usb_in_complete(void);
void dap_diagnostics_request_ring_depth(uint8_t depth);
void dap_diagnostics_swd_queued(uint8_t transfer_count);
void dap_diagnostics_rf_tx_start(uint8_t frame_length, bool retransmit);
void dap_diagnostics_rf_tx_done(void);
void dap_diagnostics_request_ack(void);
void dap_diagnostics_swd_response(void);
void dap_diagnostics_rx_restored(void);
void dap_diagnostics_burst_queued(uint8_t count, uint8_t request_bytes);
void dap_diagnostics_burst_response_bytes(uint8_t response_bytes);
void dap_diagnostics_single_swd(void);
void dap_diagnostics_burst_fallback(void);
void dap_diagnostics_burst_parse_error(void);
void dap_diagnostics_burst_reject_parse(void);
void dap_diagnostics_burst_reject_capacity(void);
void dap_diagnostics_burst_reject_bridge(void);
#define DAP_DIAG(call) dap_diagnostics_##call
#else
#define DAP_DIAG(call) ((void)0)
#endif

#endif
