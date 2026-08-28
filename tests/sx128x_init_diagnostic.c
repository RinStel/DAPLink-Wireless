/*
 * DAPLink-Wireless — SX128x 初始化诊断工具
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * 用于调试无线模块初始化失败（红灯常亮）问题
 *
 * LED 指示含义：
 * - 快速闪烁红灯（100ms）：BUSY 引脚超时，硬件连接问题
 * - 红蓝交替闪烁（200ms）：SPI 通信或初始化失败
 * - 慢速闪烁红灯（500ms）：状态验证失败
 * - 绿灯常亮：所有测试通过
 */
#include "board.h"
#include "board_pins.h"
#include "radio_hal.h"
#include "sx128x.h"
#include "gd32f30x_gpio.h"

typedef struct {
    const char *step_name;
    bool passed;
    uint32_t error_code;
    uint32_t extra_info;
} diagnostic_step_t;

#define MAX_STEPS 30
static diagnostic_step_t diagnostic_log[MAX_STEPS];
static uint8_t step_count = 0;

static void log_step(const char *name, bool passed, uint32_t error_code, uint32_t extra)
{
    if (step_count < MAX_STEPS) {
        diagnostic_log[step_count].step_name = name;
        diagnostic_log[step_count].passed = passed;
        diagnostic_log[step_count].error_code = error_code;
        diagnostic_log[step_count].extra_info = extra;
        step_count++;
    }
}

static void blink_pattern(uint32_t red_ms, uint32_t blue_ms, uint32_t green_ms, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        board_led_set(BOARD_LED_RED, red_ms > 0);
        board_led_set(BOARD_LED_BLUE, blue_ms > 0);
        board_led_set(BOARD_LED_GREEN, green_ms > 0);
        board_delay_ms(red_ms > 0 ? red_ms : (blue_ms > 0 ? blue_ms : green_ms));

        board_led_set(BOARD_LED_RED, false);
        board_led_set(BOARD_LED_BLUE, false);
        board_led_set(BOARD_LED_GREEN, false);
        board_delay_ms(100);
    }
}

/* 测试 GPIO 引脚是否正常工作 */
static void test_gpio_pins(void)
{
    uint32_t nreset_state, busy_state, nss_state;

    /* 测试 NRESET 引脚控制 */
    gpio_bit_set(BOARD_RF_NRESET_PORT, BOARD_RF_NRESET_PIN);
    board_delay_ms(1);
    nreset_state = gpio_output_bit_get(BOARD_RF_NRESET_PORT, BOARD_RF_NRESET_PIN);
    log_step("NRESET High", nreset_state == SET, nreset_state, 0);

    gpio_bit_reset(BOARD_RF_NRESET_PORT, BOARD_RF_NRESET_PIN);
    board_delay_ms(1);
    nreset_state = gpio_output_bit_get(BOARD_RF_NRESET_PORT, BOARD_RF_NRESET_PIN);
    log_step("NRESET Low", nreset_state == RESET, nreset_state, 0);

    /* 测试 NSS 引脚控制 */
    gpio_bit_set(BOARD_RF_NSS_PORT, BOARD_RF_NSS_PIN);
    board_delay_ms(1);
    nss_state = gpio_output_bit_get(BOARD_RF_NSS_PORT, BOARD_RF_NSS_PIN);
    log_step("NSS High", nss_state == SET, nss_state, 0);

    /* 读取 BUSY 引脚初始状态 */
    busy_state = gpio_input_bit_get(BOARD_RF_BUSY_PORT, BOARD_RF_BUSY_PIN);
    log_step("BUSY Initial", true, busy_state, 0);
}

void sx128x_init_diagnostic(void)
{
    radio_result_t radio_result;
    sx128x_result_t sx_result;
    sx128x_status_t status;
    uint16_t irq_status;
    uint8_t retry;

    step_count = 0;

    /* 步骤 1: 板级初始化 */
    board_init();
    log_step("Board Init", true, 0, 0);

    /* 开启蓝灯表示诊断进行中 */
    board_led_set(BOARD_LED_BLUE, true);
    board_delay_ms(500);
    board_led_set(BOARD_LED_BLUE, false);

    /* 步骤 2: GPIO 引脚测试 */
    test_gpio_pins();

    /* 步骤 3: Radio HAL 初始化（包含多次重试） */
    radio_result = radio_hal_init();
    log_step("Radio HAL Init", radio_result == RADIO_RESULT_OK, radio_result, 0);

    if (radio_result != RADIO_RESULT_OK) {
        goto diagnostic_complete;
    }

    /* 步骤 4: 多次尝试获取芯片状态 */
    for (retry = 0; retry < 3; retry++) {
        sx_result = sx128x_get_status(&status);
        if (sx_result == SX128X_RESULT_OK) {
            break;
        }
        board_delay_ms(10);
    }
    log_step("Get Status", sx_result == SX128X_RESULT_OK, sx_result, retry);

    if (sx_result == SX128X_RESULT_OK) {
        log_step("Status Mode", status.mode != SX128X_MODE_RESERVED, status.mode, status.raw);
        log_step("Command Status", status.command_status < 3, status.command_status, status.raw);
    } else {
        goto diagnostic_complete;
    }

    /* 步骤 5: 进入 Standby 模式 */
    sx_result = sx128x_standby();
    log_step("Enter Standby", sx_result == SX128X_RESULT_OK, sx_result, 0);

    if (sx_result != SX128X_RESULT_OK) {
        goto diagnostic_complete;
    }

    /* 步骤 6: 确认 Standby 模式 */
    board_delay_ms(5);
    sx_result = sx128x_get_status(&status);
    log_step("Get Status 2", sx_result == SX128X_RESULT_OK, sx_result, 0);

    if (sx_result == SX128X_RESULT_OK) {
        log_step("Verify Standby", status.mode == SX128X_MODE_STDBY_RC, status.mode, status.raw);
    }

    /* 步骤 7: 完整初始化 GFSK */
    sx_result = sx128x_init_gfsk();
    log_step("Init GFSK", sx_result == SX128X_RESULT_OK, sx_result, 0);

    if (sx_result != SX128X_RESULT_OK) {
        goto diagnostic_complete;
    }

    /* 步骤 8: 获取中断状态 */
    sx_result = sx128x_get_irq_status(&irq_status);
    log_step("Get IRQ Status", sx_result == SX128X_RESULT_OK, sx_result, irq_status);

    /* 步骤 9: 清除中断 */
    sx_result = sx128x_clear_irq_status(SX128X_IRQ_ALL);
    log_step("Clear IRQ", sx_result == SX128X_RESULT_OK, sx_result, 0);

    /* 步骤 10: 最终状态确认 */
    sx_result = sx128x_get_status(&status);
    log_step("Final Status", sx_result == SX128X_RESULT_OK, sx_result, 0);
    if (sx_result == SX128X_RESULT_OK) {
        log_step("Final Mode", status.mode == SX128X_MODE_STDBY_RC, status.mode, status.raw);
    }

diagnostic_complete:
    /* 通过 LED 闪烁显示诊断结果 */
    board_delay_ms(500);

    /* 闪烁显示步骤数（蓝灯） */
    blink_pattern(0, 200, 0, step_count / 5);
    board_delay_ms(1000);

    /* 找到第一个失败的步骤并指示 */
    uint8_t first_fail = 0xFF;
    for (uint8_t i = 0; i < step_count; i++) {
        if (!diagnostic_log[i].passed) {
            first_fail = i;
            break;
        }
    }

    if (first_fail == 0xFF) {
        /* 所有测试通过 - 绿灯常亮 */
        board_led_set(BOARD_LED_GREEN, true);
        while (1) {
            board_delay_ms(1000);
        }
    } else if (first_fail <= 5) {
        /* 早期失败（GPIO/HAL 初始化）- 快速闪烁红灯 */
        blink_pattern(100, 0, 0, 5);
        board_delay_ms(1000);
        /* 显示错误码（红灯闪烁次数） */
        blink_pattern(300, 0, 0, diagnostic_log[first_fail].error_code);
        while (1) {
            board_led_set(BOARD_LED_RED, true);
            board_delay_ms(100);
            board_led_set(BOARD_LED_RED, false);
            board_delay_ms(100);
        }
    } else if (first_fail <= 10) {
        /* 中期失败（状态/Standby）- 慢速闪烁红灯 */
        blink_pattern(500, 0, 0, 3);
        board_delay_ms(1000);
        while (1) {
            board_led_set(BOARD_LED_RED, true);
            board_delay_ms(500);
            board_led_set(BOARD_LED_RED, false);
            board_delay_ms(500);
        }
    } else {
        /* 后期失败（初始化/验证）- 红蓝交替 */
        while (1) {
            board_led_set(BOARD_LED_RED, true);
            board_led_set(BOARD_LED_BLUE, false);
            board_delay_ms(200);
            board_led_set(BOARD_LED_RED, false);
            board_led_set(BOARD_LED_BLUE, true);
            board_delay_ms(200);
        }
    }
}
