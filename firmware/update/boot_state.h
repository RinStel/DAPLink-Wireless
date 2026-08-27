#ifndef BOOT_STATE_H
#define BOOT_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "firmware_layout.h"

typedef enum {
    BOOT_PHASE_CONFIRMED = 0U,
    BOOT_PHASE_PENDING_TEST = 1U
} boot_phase_t;

typedef struct {
    uint32_t generation;
    firmware_slot_t confirmed_slot;
    uint32_t confirmed_version;
    uint32_t confirmed_length;
    uint32_t confirmed_crc32;
    firmware_slot_t pending_slot;
    uint32_t pending_version;
    uint32_t pending_length;
    uint32_t pending_crc32;
    boot_phase_t phase;
    uint32_t attempts_used;
} boot_state_t;

typedef enum {
    BOOT_ATTEMPT_NOT_PENDING = 0U,
    BOOT_ATTEMPT_1 = 1U,
    BOOT_ATTEMPT_2 = 2U,
    BOOT_ATTEMPT_3 = 3U,
    BOOT_ATTEMPTS_EXHAUSTED = 4U
} boot_attempt_result_t;

typedef struct {
    bool (*read)(uint32_t address, void *data, size_t length);
    bool (*erase_page)(uint32_t address);
    bool (*program_word)(uint32_t address, uint32_t value);
} boot_state_flash_ops_t;

void boot_state_flash_set_ops(const boot_state_flash_ops_t *ops);
bool boot_state_load(boot_state_t *state);
bool boot_state_factory_init(firmware_slot_t confirmed_slot,
                             uint32_t confirmed_version,
                             uint32_t image_length,
                             uint32_t image_crc32);
bool boot_state_set_pending(firmware_slot_t pending_slot,
                            uint32_t pending_version,
                            uint32_t image_length,
                            uint32_t image_crc32);
boot_attempt_result_t boot_state_consume_attempt(boot_state_t *state);
bool boot_state_confirm(firmware_slot_t running_slot);
bool boot_state_rollback(void);

#endif
