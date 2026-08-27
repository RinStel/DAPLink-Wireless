#ifndef BOOT_LED_H
#define BOOT_LED_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool red;
    bool green;
    bool blue;
} boot_led_rgb_t;

typedef enum {
    BOOT_LED_DFU_IDLE = 0U,
    BOOT_LED_DFU_WRITE,
    BOOT_LED_VERIFY,
    BOOT_LED_SUCCESS,
    BOOT_LED_ERROR_HEADER,
    BOOT_LED_ERROR_FLASH,
    BOOT_LED_TRIAL,
    BOOT_LED_ROLLBACK,
    BOOT_LED_FATAL
} boot_led_state_t;

boot_led_rgb_t boot_led_update(boot_led_state_t state, uint32_t now_ms);

#endif
