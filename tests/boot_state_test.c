#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "boot_state.h"
#include "firmware_image.h"

#define FLASH_BASE 0x08000000U
#define FLASH_SIZE (256U * 1024U)
#define PAGE_SIZE  2048U

static uint8_t s_flash[FLASH_SIZE];
static int32_t s_fail_erase_at;
static int32_t s_fail_program_at;
static uint32_t s_erase_calls;
static uint32_t s_program_calls;

static bool flash_range(uint32_t address, size_t length, size_t *offset)
{
    if ((address < FLASH_BASE) ||
        ((uint64_t)address + length > (uint64_t)FLASH_BASE + FLASH_SIZE)) {
        return false;
    }
    *offset = address - FLASH_BASE;
    return true;
}

bool boot_state_test_read(uint32_t address, void *data, size_t length)
{
    size_t offset;
    if ((data == NULL) || !flash_range(address, length, &offset)) {
        return false;
    }
    memcpy(data, &s_flash[offset], length);
    return true;
}

bool boot_state_test_erase(uint32_t address)
{
    size_t offset;
    if (((address % PAGE_SIZE) != 0U) ||
        !flash_range(address, PAGE_SIZE, &offset)) {
        return false;
    }
    if ((s_fail_erase_at >= 0) &&
        (s_erase_calls == (uint32_t)s_fail_erase_at)) {
        return false;
    }
    ++s_erase_calls;
    memset(&s_flash[offset], 0xFF, PAGE_SIZE);
    return true;
}

bool boot_state_test_program(uint32_t address, uint32_t value)
{
    size_t offset;
    uint32_t current;
    if (((address & 3U) != 0U) ||
        !flash_range(address, sizeof(value), &offset)) {
        return false;
    }
    if ((s_fail_program_at >= 0) &&
        (s_program_calls == (uint32_t)s_fail_program_at)) {
        return false;
    }
    ++s_program_calls;
    memcpy(&current, &s_flash[offset], sizeof(current));
    if ((current & value) != value) {
        return false;
    }
    current &= value;
    memcpy(&s_flash[offset], &current, sizeof(current));
    return true;
}

static void reset_failures(void)
{
    s_fail_erase_at = -1;
    s_fail_program_at = -1;
    s_erase_calls = 0U;
    s_program_calls = 0U;
}

int main(void)
{
    boot_state_t state;

    memset(s_flash, 0xFF, sizeof(s_flash));
    reset_failures();
    assert(!boot_state_load(&state));
    assert(boot_state_factory_init(FIRMWARE_SLOT_A, 799U,
                                   47512U, 0x12345678U));
    assert(boot_state_load(&state));
    assert(state.confirmed_slot == FIRMWARE_SLOT_A);
    assert(state.phase == BOOT_PHASE_CONFIRMED);

    assert(boot_state_set_pending(FIRMWARE_SLOT_B, 800U,
                                  48000U, 0x87654321U));
    assert(boot_state_load(&state));
    assert(state.phase == BOOT_PHASE_PENDING_TEST);
    assert(state.pending_slot == FIRMWARE_SLOT_B);
    assert(state.attempts_used == 0U);
    assert(boot_state_consume_attempt(&state) == BOOT_ATTEMPT_1);
    assert(boot_state_consume_attempt(&state) == BOOT_ATTEMPT_2);
    assert(boot_state_consume_attempt(&state) == BOOT_ATTEMPT_3);
    assert(boot_state_consume_attempt(&state) == BOOT_ATTEMPTS_EXHAUSTED);

    assert(boot_state_rollback());
    assert(boot_state_load(&state));
    assert(state.confirmed_slot == FIRMWARE_SLOT_A);
    assert(state.phase == BOOT_PHASE_CONFIRMED);

    assert(boot_state_set_pending(FIRMWARE_SLOT_B, 800U,
                                  48000U, 0x87654321U));
    assert(boot_state_confirm(FIRMWARE_SLOT_B));
    assert(boot_state_load(&state));
    assert(state.confirmed_slot == FIRMWARE_SLOT_B);
    assert(state.confirmed_version == 800U);

    memset(s_flash, 0xFF, sizeof(s_flash));
    reset_failures();
    s_fail_erase_at = 0;
    assert(!boot_state_factory_init(FIRMWARE_SLOT_A, 799U,
                                    47512U, 0x12345678U));
    assert(!boot_state_load(&state));

    memset(s_flash, 0xFF, sizeof(s_flash));
    reset_failures();
    assert(boot_state_factory_init(FIRMWARE_SLOT_A, 799U,
                                   47512U, 0x12345678U));
    reset_failures();
    s_fail_program_at = 2;
    assert(!boot_state_set_pending(FIRMWARE_SLOT_B, 800U,
                                   48000U, 0x87654321U));
    assert(boot_state_load(&state));
    assert(state.phase == BOOT_PHASE_CONFIRMED);
    return 0;
}
