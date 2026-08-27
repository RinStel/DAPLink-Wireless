#ifndef BOOT_BOARD_H
#define BOOT_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_led.h"

void boot_board_init(void);
uint32_t boot_board_millis(void);
void boot_board_systick_isr(void);
bool boot_board_key_held(uint32_t stable_ms);
void boot_board_set_led(boot_led_rgb_t leds);
void boot_board_usb_connect(bool connect);
void boot_board_system_reset(void);
void boot_board_jump_to_application(uint32_t vector_base);

#endif
