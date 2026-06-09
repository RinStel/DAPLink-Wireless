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
