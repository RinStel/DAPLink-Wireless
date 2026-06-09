#ifndef SERIAL_SERVICE_H
#define SERIAL_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "device_config.h"
#include "radio_protocol.h"

bool serial_service_init(void);
void serial_service_process(void);
bool serial_service_wired_process(void);
bool serial_service_deliver_data(device_mode_t mode,
                                 const uint8_t *payload, uint8_t length);
bool serial_service_deliver_line_coding(const uint8_t *payload,
                                        uint8_t length);
bool serial_service_source_take(device_mode_t mode,
                                radio_frame_type_t *type,
                                uint8_t *payload, uint8_t *length);
uint32_t serial_service_rx_overruns(void);

#endif
