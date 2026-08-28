/*
 * DAPLink-Wireless — Wireless CMSIS-DAP v2 debug probe firmware
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#ifndef STATUS_INDICATOR_H
#define STATUS_INDICATOR_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* 逻辑 LED 状态；board.c 负责低电平有效的硬件极性。 */
    bool red;
    bool green;
    bool blue;
} status_indicator_leds_t;

typedef enum {
    STATUS_INDICATOR_MODE_WIRED = 0,
    STATUS_INDICATOR_MODE_WIRELESS_HOST,
    STATUS_INDICATOR_MODE_WIRELESS_SLAVE
} status_indicator_mode_t;

typedef enum {
    STATUS_INDICATOR_ACTIVITY_NONE = 0,
    STATUS_INDICATOR_ACTIVITY_COMMUNICATION,
    STATUS_INDICATOR_ACTIVITY_PROGRAMMING
} status_indicator_activity_t;

typedef enum {
    STATUS_INDICATOR_INITIALIZATION_FAILURE = 0,
    STATUS_INDICATOR_RUNTIME_ERROR,
    STATUS_INDICATOR_HEALTHY
} status_indicator_state_t;

typedef struct {
    status_indicator_state_t state;
    /* 恢复计时使用 board_millis() 单位。 */
    uint32_t recovery_started_at_ms;
    bool recovery_pending;
} status_indicator_t;

void status_indicator_init(status_indicator_t *indicator,
                           bool initialization_failed);
status_indicator_leds_t status_indicator_update(status_indicator_t *indicator,
                                                bool runtime_error,
                                                status_indicator_mode_t mode,
                                                status_indicator_activity_t activity,
                                                uint32_t now_ms);

#endif
