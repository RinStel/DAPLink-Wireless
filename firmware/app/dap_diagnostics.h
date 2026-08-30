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
void dap_diagnostics_rf_tx_start(uint8_t frame_length, bool retransmit);
void dap_diagnostics_rf_tx_done(void);
void dap_diagnostics_request_ack(void);
void dap_diagnostics_swd_response(void);
void dap_diagnostics_rx_restored(void);
#define DAP_DIAG(call) dap_diagnostics_##call
#else
#define DAP_DIAG(call) ((void)0)
#endif

#endif
