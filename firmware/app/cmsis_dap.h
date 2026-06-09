#ifndef CMSIS_DAP_H
#define CMSIS_DAP_H

#include <stdbool.h>
#include <stdint.h>

#define CMSIS_DAP_PACKET_SIZE 64U

void cmsis_dap_init(void);
bool cmsis_dap_submit(const uint8_t *request, uint8_t length);
void cmsis_dap_abort(void);
void cmsis_dap_process(void);
bool cmsis_dap_busy(void);
bool cmsis_dap_response_take(uint8_t *response, uint8_t *length);

#endif
