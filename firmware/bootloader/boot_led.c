#include "boot_led.h"

#define BOOT_LED_ROLLBACK_MS 2000U

bool boot_led_rollback_active(uint32_t started_at_ms, uint32_t now_ms)
{
    return (uint32_t)(now_ms - started_at_ms) < BOOT_LED_ROLLBACK_MS;
}

boot_led_rgb_t boot_led_update(boot_led_state_t state, uint32_t now_ms)
{
    boot_led_rgb_t leds = {false, false, false};
    bool slow = ((now_ms / 500U) % 2U) == 0U;
    bool fast = ((now_ms / 125U) % 2U) == 0U;

    switch (state) {
    case BOOT_LED_DFU_IDLE:
        leds.blue = slow;
        break;
    case BOOT_LED_DFU_WRITE:
        leds.blue = fast;
        break;
    case BOOT_LED_VERIFY:
        leds.blue = fast;
        leds.green = fast;
        break;
    case BOOT_LED_SUCCESS:
        leds.green = true;
        break;
    case BOOT_LED_ERROR_HEADER:
        leds.red = slow;
        break;
    case BOOT_LED_ERROR_FLASH:
    case BOOT_LED_FATAL:
        leds.red = fast;
        break;
    case BOOT_LED_TRIAL:
        leds.green = fast;
        break;
    case BOOT_LED_ROLLBACK:
        leds.red = ((now_ms / 200U) % 2U) == 0U;
        leds.blue = !leds.red;
        break;
    default:
        break;
    }
    return leds;
}
