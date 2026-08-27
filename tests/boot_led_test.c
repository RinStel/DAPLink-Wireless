#include <assert.h>

#include "boot_led.h"

int main(void)
{
    boot_led_rgb_t leds;

    leds = boot_led_update(BOOT_LED_DFU_IDLE, 0U);
    assert(!leds.red && !leds.green && leds.blue);
    leds = boot_led_update(BOOT_LED_DFU_IDLE, 600U);
    assert(!leds.red && !leds.green && !leds.blue);
    leds = boot_led_update(BOOT_LED_DFU_WRITE, 0U);
    assert(leds.blue);
    leds = boot_led_update(BOOT_LED_VERIFY, 0U);
    assert(leds.blue && leds.green);
    leds = boot_led_update(BOOT_LED_SUCCESS, 0U);
    assert(leds.green && !leds.red && !leds.blue);
    leds = boot_led_update(BOOT_LED_FATAL, 0U);
    assert(leds.red && !leds.green && !leds.blue);
    return 0;
}
