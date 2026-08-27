#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "boot_board.h"
#include "boot_led.h"
#include "boot_mailbox.h"
#include "boot_policy.h"
#include "boot_state.h"
#include "dfu_device.h"
#include "firmware_image.h"
#include "gd32f30x_misc.h"
#include "gd32f30x_rcu.h"
#include "usbd_lld_int.h"

#define BOOT_KEY_STABLE_MS       50U
#define BOOT_SUCCESS_LED_MS      500U
#define BOOT_USB_CONNECT_DELAY_MS 10U

static uint32_t crc32_update(uint32_t crc, const uint8_t *data,
                             uint32_t length)
{
    uint32_t index;

    for (index = 0U; index < length; ++index) {
        uint8_t bit;

        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return crc;
}

static bool slot_crc_valid(firmware_slot_t slot, uint32_t length,
                           uint32_t expected_crc)
{
    uint8_t buffer[64];
    uint32_t offset = 0U;
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t base = firmware_slot_base(slot);

    while (offset < length) {
        uint32_t count = length - offset;

        if (count > sizeof(buffer)) {
            count = sizeof(buffer);
        }
        memcpy(buffer, (const void *)(base + offset), count);
        crc = crc32_update(crc, buffer, count);
        offset += count;
    }
    return ~crc == expected_crc;
}

static bool slot_valid(firmware_slot_t slot, uint32_t length,
                       uint32_t expected_crc)
{
    uint32_t base;
    uint32_t initial_msp;
    uint32_t reset_vector;

    if ((slot > FIRMWARE_SLOT_B) || (length < 8U) ||
        (length > FIRMWARE_SLOT_SIZE)) {
        return false;
    }
    base = firmware_slot_base(slot);
    if ((uint64_t)base + length > FIRMWARE_FLASH_END) {
        return false;
    }
    memcpy(&initial_msp, (const void *)base, sizeof(initial_msp));
    memcpy(&reset_vector, (const void *)(base + sizeof(initial_msp)),
           sizeof(reset_vector));
    return firmware_image_vectors_validate(base, length, initial_msp,
                                           reset_vector) &&
           slot_crc_valid(slot, length, expected_crc);
}

static bool state_slot_valid(const boot_state_t *state, firmware_slot_t slot,
                             bool pending)
{
    if (state == NULL) {
        return false;
    }
    if (pending) {
        return slot_valid(slot, state->pending_length,
                          state->pending_crc32);
    }
    return slot_valid(slot, state->confirmed_length,
                      state->confirmed_crc32);
}

static void boot_usb_irq_enable(bool enable)
{
    if (enable) {
        nvic_irq_enable(USBD_LP_CAN0_RX0_IRQn, 1U, 0U);
        nvic_irq_enable(USBD_HP_CAN0_TX_IRQn, 1U, 0U);
        nvic_irq_enable(USBD_WKUP_IRQn, 1U, 0U);
    } else {
        nvic_irq_disable(USBD_LP_CAN0_RX0_IRQn);
        nvic_irq_disable(USBD_HP_CAN0_TX_IRQn);
        nvic_irq_disable(USBD_WKUP_IRQn);
    }
}

static void boot_usb_start(dfu_device_t *device)
{
    static usb_dev usb;

    rcu_usb_clock_config(RCU_CKUSB_CKPLL_DIV2_5);
    rcu_periph_clock_enable(RCU_USBD);
    dfu_device_usb_bind(device);
    dfu_device_usb_init(&usb);
    boot_board_usb_connect(true);
    boot_usb_irq_enable(true);
    usbd_connect(&usb);
    boot_board_set_led(boot_led_update(BOOT_LED_DFU_IDLE,
                                       boot_board_millis()));
    while (!dfu_device_usb_manifest_status_sent()) {
        dfu_device_usb_irq();
        boot_board_set_led(boot_led_update(
            dfu_device_state(device) == DFU_STATE_ERROR
                ? ((device->status == DFU_STATUS_ERR_FILE) ||
                   (device->status == DFU_STATUS_ERR_ADDRESS) ||
                   (device->status == DFU_STATUS_ERR_FIRMWARE) ||
                   (device->status == DFU_STATUS_ERR_SEQUENCE)
                       ? BOOT_LED_ERROR_HEADER : BOOT_LED_ERROR_FLASH)
                : ((dfu_device_state(device) == DFU_STATE_MANIFEST_SYNC) ||
                   (dfu_device_state(device) == DFU_STATE_MANIFEST) ||
                   (dfu_device_state(device) ==
                    DFU_STATE_MANIFEST_WAIT_RESET)
                       ? BOOT_LED_VERIFY
                       : (dfu_device_state(device) == DFU_STATE_DNLOAD_SYNC
                              ? BOOT_LED_DFU_WRITE
                              : BOOT_LED_DFU_IDLE)),
            boot_board_millis()));
        if (dfu_device_manifest_complete(device) &&
            dfu_device_usb_manifest_status_sent()) {
            break;
        }
    }
    if (!dfu_device_manifest_complete(device) ||
        !dfu_device_usb_manifest_status_sent()) {
        /* Error states intentionally remain enumerated forever. */
        for (;;) {
            dfu_device_usb_irq();
            boot_board_set_led(boot_led_update(BOOT_LED_ERROR_FLASH,
                                               boot_board_millis()));
        }
    }
    boot_board_set_led(boot_led_update(BOOT_LED_SUCCESS,
                                       boot_board_millis()));
    {
        uint32_t deadline = boot_board_millis() + BOOT_SUCCESS_LED_MS;
        while ((uint32_t)(boot_board_millis() - deadline) >= 0x80000000U) {
        /* Keep the USB stack alive until the successful status handshake has
         * been visible to the host. */
            boot_board_set_led(boot_led_update(BOOT_LED_SUCCESS,
                                               boot_board_millis()));
        }
    }
    boot_usb_irq_enable(false);
    boot_board_usb_connect(false);
    boot_board_system_reset();
}

static void boot_enter_dfu(firmware_slot_t active_slot,
                           uint32_t confirmed_version, bool recovery)
{
    dfu_device_t device;

    dfu_device_init(&device, active_slot, confirmed_version, recovery);
    boot_usb_start(&device);
}

static void boot_start_slot(firmware_slot_t slot)
{
    boot_led_state_t state = BOOT_LED_TRIAL;

    boot_board_set_led(boot_led_update(state, boot_board_millis()));
    boot_board_jump_to_application(firmware_slot_base(slot));
}

int main(void)
{
    boot_state_t state;
    boot_policy_input_t input;
    boot_decision_t decision;
    bool state_valid;
    bool key_requested;
    bool mailbox_requested;

    boot_board_init();
    key_requested = boot_board_key_held(BOOT_KEY_STABLE_MS);
    mailbox_requested = boot_mailbox_take_dfu_request();
    state_valid = boot_state_load(&state);

    memset(&input, 0, sizeof(input));
    input.key_recovery_requested = key_requested;
    input.mailbox_dfu_requested = mailbox_requested;
    input.state_valid = state_valid;
    if (state_valid) {
        input.state = state;
        input.attempts_used = state.attempts_used;
        input.confirmed_image_valid = state_slot_valid(
            &state, state.confirmed_slot, false);
        if (state.phase == BOOT_PHASE_PENDING_TEST) {
            input.pending_image_valid = state_slot_valid(
                &state, state.pending_slot, true);
        }
    }
    decision = boot_policy_decide(&input);

    if ((decision.action == BOOT_ACTION_DFU_RECOVERY) ||
        (decision.action == BOOT_ACTION_DFU_NORMAL)) {
        boot_enter_dfu(state_valid ? state.confirmed_slot : FIRMWARE_SLOT_NONE,
                       state_valid ? state.confirmed_version : 0U,
                       decision.action == BOOT_ACTION_DFU_RECOVERY);
    }
    if (decision.action == BOOT_ACTION_ROLLBACK_TO_A ||
        decision.action == BOOT_ACTION_ROLLBACK_TO_B) {
        boot_board_set_led(boot_led_update(BOOT_LED_ROLLBACK,
                                           boot_board_millis()));
        if (boot_state_rollback() && boot_state_load(&state)) {
            boot_start_slot(state.confirmed_slot);
        }
        boot_enter_dfu(FIRMWARE_SLOT_NONE, 0U, true);
    }
    if ((decision.action == BOOT_ACTION_START_A) ||
        (decision.action == BOOT_ACTION_START_B)) {
        firmware_slot_t slot = decision.action == BOOT_ACTION_START_A
                                   ? FIRMWARE_SLOT_A : FIRMWARE_SLOT_B;
        if (state_valid && state.phase == BOOT_PHASE_PENDING_TEST &&
            state.pending_slot == slot) {
            if (boot_state_consume_attempt(&state) == BOOT_ATTEMPTS_EXHAUSTED) {
                (void)boot_state_rollback();
                boot_enter_dfu(FIRMWARE_SLOT_NONE, 0U, true);
            }
        }
        boot_start_slot(slot);
    }
    boot_enter_dfu(FIRMWARE_SLOT_NONE, 0U, true);
    return 0;
}

void USBD_LP_CAN0_RX0_IRQHandler(void)
{
    dfu_device_usb_irq();
}

void USBD_HP_CAN0_TX_IRQHandler(void)
{
    dfu_device_usb_hp_irq();
}

void USBD_WKUP_IRQHandler(void)
{
    extern usb_dev *dfu_device_usb_current(void);
    dfu_device_usb_wakeup_irq(dfu_device_usb_current());
}
