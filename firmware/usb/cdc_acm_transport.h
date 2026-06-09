#ifndef CDC_ACM_TRANSPORT_H
#define CDC_ACM_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "cdc_acm_core.h"

extern usb_class cdc_class;

uint16_t cdc_acm_read(uint8_t *data, uint16_t capacity);
uint16_t cdc_acm_write(const uint8_t *data, uint16_t length);
bool cdc_acm_tx_ready(void);
bool cdc_acm_line_coding_take(acm_line *line);

#endif
