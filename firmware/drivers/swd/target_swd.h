#ifndef TARGET_SWD_H
#define TARGET_SWD_H

#include <stdbool.h>
#include <stdint.h>

#define SWD_SEQUENCE_MAX_RESPONSE 60U

typedef enum {
    TARGET_SWD_ACK_OK = 1,
    TARGET_SWD_ACK_WAIT = 2,
    TARGET_SWD_ACK_FAULT = 4,
    TARGET_SWD_ACK_PROTOCOL = 7,
    TARGET_SWD_ACK_PARITY = 8
} target_swd_ack_t;

void target_swd_init(uint32_t clock_hz);
void target_swd_configure(uint8_t idle_cycles, uint16_t retry_count,
                          uint8_t turnaround, bool data_phase);
void target_swd_disconnect(void);
bool target_swd_connect(uint32_t *idcode);
target_swd_ack_t target_swd_transfer(uint8_t request, uint32_t *data);
void target_swd_abort_request(void);
void target_swd_abort_clear(void);
bool target_swd_sequence(uint16_t bit_count, const uint8_t *data);
bool target_swd_sequence_transfer(const uint8_t *request,
                                  uint8_t request_length,
                                  uint8_t *response,
                                  uint8_t *response_length);
void target_swd_pins_set(uint8_t value, uint8_t select);
uint8_t target_swd_pins_read(void);
void target_swd_reset_pulse(uint32_t duration_ms);

#endif
