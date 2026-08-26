#include "boot_state.h"

#include <string.h>

#include "firmware_image.h"

#define BOOT_STATE_MAGIC       0x42535441U
#define BOOT_STATE_VERSION     1U
#define BOOT_STATE_RECORD_SIZE 64U
#define BOOT_STATE_COMMIT      0x434F4D54U
#define BOOT_STATE_ATTEMPT_MASK_OFFSET 32U
#define BOOT_STATE_CRC_OFFSET  36U
#define BOOT_STATE_COMMIT_OFFSET 40U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t generation;
    uint8_t confirmed_slot;
    uint8_t pending_slot;
    uint8_t phase;
    uint8_t reserved0;
    uint32_t confirmed_version;
    uint32_t pending_version;
    uint32_t pending_length;
    uint32_t pending_crc32;
    uint32_t attempt_mask;
    uint32_t record_crc32;
    uint32_t commit;
    uint8_t reserved1[20];
} boot_state_record_t;

_Static_assert(sizeof(boot_state_record_t) == BOOT_STATE_RECORD_SIZE,
               "boot state record must be 64 bytes");

#ifdef BOOT_STATE_HOST_TEST
extern bool boot_state_test_read(uint32_t address, void *data, size_t length);
extern bool boot_state_test_erase(uint32_t address);
extern bool boot_state_test_program(uint32_t address, uint32_t value);
#include <stddef.h>
#else
#include "gd32f30x_fmc.h"
#endif

static boot_state_flash_ops_t s_ops;
static uint32_t s_active_address;
static bool s_active_valid;

static bool flash_read(uint32_t address, void *data, size_t length)
{
    if (s_ops.read != NULL) {
        return s_ops.read(address, data, length);
    }
#ifdef BOOT_STATE_HOST_TEST
    return boot_state_test_read(address, data, length);
#else
    memcpy(data, (const void *)address, length);
    return true;
#endif
}

static bool flash_erase(uint32_t address)
{
    if (s_ops.erase_page != NULL) {
        return s_ops.erase_page(address);
    }
#ifdef BOOT_STATE_HOST_TEST
    return boot_state_test_erase(address);
#else
    return fmc_page_erase(address) == FMC_READY;
#endif
}

static bool flash_program(uint32_t address, uint32_t value)
{
    if (s_ops.program_word != NULL) {
        return s_ops.program_word(address, value);
    }
#ifdef BOOT_STATE_HOST_TEST
    return boot_state_test_program(address, value);
#else
    return fmc_word_program(address, value) == FMC_READY;
#endif
}

void boot_state_flash_set_ops(const boot_state_flash_ops_t *ops)
{
    if (ops == NULL) {
        memset(&s_ops, 0, sizeof(s_ops));
    } else {
        s_ops = *ops;
    }
}

static uint32_t state_address(uint8_t index)
{
    return FIRMWARE_BOOT_STATE_BASE + (uint32_t)index *
           FIRMWARE_FLASH_PAGE_SIZE;
}

static bool record_decode(uint32_t address, boot_state_record_t *record)
{
    uint32_t crc;

    if (!flash_read(address, record, sizeof(*record))) {
        return false;
    }
    if ((record->magic != BOOT_STATE_MAGIC) ||
        (record->version != BOOT_STATE_VERSION) ||
        (record->size != BOOT_STATE_RECORD_SIZE) ||
        (record->commit != BOOT_STATE_COMMIT) ||
        (record->confirmed_slot > (uint8_t)FIRMWARE_SLOT_B) ||
        (record->pending_slot > (uint8_t)FIRMWARE_SLOT_B) ||
        (record->phase > (uint8_t)BOOT_PHASE_PENDING_TEST)) {
        return false;
    }
    crc = firmware_crc32(record, BOOT_STATE_ATTEMPT_MASK_OFFSET);
    return crc == record->record_crc32;
}

static void record_to_state(const boot_state_record_t *record,
                            boot_state_t *state)
{
    state->generation = record->generation;
    state->confirmed_slot = (firmware_slot_t)record->confirmed_slot;
    state->confirmed_version = record->confirmed_version;
    state->pending_slot = (firmware_slot_t)record->pending_slot;
    state->pending_version = record->pending_version;
    state->pending_length = record->pending_length;
    state->pending_crc32 = record->pending_crc32;
    state->phase = (boot_phase_t)record->phase;
    state->attempts_used = 3U;
    if ((record->attempt_mask & 1U) != 0U) {
        state->attempts_used = 0U;
    } else if ((record->attempt_mask & 2U) != 0U) {
        state->attempts_used = 1U;
    } else if ((record->attempt_mask & 4U) != 0U) {
        state->attempts_used = 2U;
    }
}

static bool latest_record(boot_state_record_t *latest, uint32_t *address)
{
    boot_state_record_t first;
    boot_state_record_t second;
    bool valid_first = record_decode(state_address(0U), &first);
    bool valid_second = record_decode(state_address(1U), &second);

    if (!valid_first && !valid_second) {
        return false;
    }
    if (valid_second && (!valid_first ||
        ((int32_t)(second.generation - first.generation) > 0))) {
        *latest = second;
        *address = state_address(1U);
    } else {
        *latest = first;
        *address = state_address(0U);
    }
    return true;
}

bool boot_state_load(boot_state_t *state)
{
    boot_state_record_t record;
    uint32_t address;

    if ((state == NULL) || !latest_record(&record, &address)) {
        s_active_valid = false;
        return false;
    }
    record_to_state(&record, state);
    s_active_address = address;
    s_active_valid = true;
    return true;
}

static bool record_write(const boot_state_record_t *record,
                         uint32_t address)
{
    uint32_t word;
    uint32_t offset;

    if (!flash_erase(address)) {
        return false;
    }
    for (offset = 0U; offset < BOOT_STATE_COMMIT_OFFSET;
         offset += sizeof(uint32_t)) {
        memcpy(&word, (const uint8_t *)record + offset, sizeof(word));
        if (!flash_program(address + offset, word)) {
            return false;
        }
    }
    if (!flash_program(address + BOOT_STATE_COMMIT_OFFSET,
                       BOOT_STATE_COMMIT)) {
        return false;
    }
    return record_decode(address, &(boot_state_record_t){0});
}

static bool save_state(const boot_state_t *state)
{
    boot_state_record_t record;
    uint32_t target;

    memset(&record, 0xFF, sizeof(record));
    record.magic = BOOT_STATE_MAGIC;
    record.version = BOOT_STATE_VERSION;
    record.size = BOOT_STATE_RECORD_SIZE;
    record.generation = state->generation;
    record.confirmed_slot = (uint8_t)state->confirmed_slot;
    record.pending_slot = (uint8_t)state->pending_slot;
    record.phase = (uint8_t)state->phase;
    record.confirmed_version = state->confirmed_version;
    record.pending_version = state->pending_version;
    record.pending_length = state->pending_length;
    record.pending_crc32 = state->pending_crc32;
    record.attempt_mask = 0xFFFFFFFFU;
    record.record_crc32 = firmware_crc32(&record,
                                         BOOT_STATE_ATTEMPT_MASK_OFFSET);
    target = s_active_valid && (s_active_address == state_address(0U))
             ? state_address(1U) : state_address(0U);
#ifndef BOOT_STATE_HOST_TEST
    fmc_unlock();
#endif
    if (!record_write(&record, target)) {
#ifndef BOOT_STATE_HOST_TEST
        fmc_lock();
#endif
        return false;
    }
#ifndef BOOT_STATE_HOST_TEST
    fmc_lock();
#endif
    s_active_address = target;
    s_active_valid = true;
    return true;
}

bool boot_state_factory_init(firmware_slot_t confirmed_slot,
                             uint32_t confirmed_version,
                             uint32_t image_length,
                             uint32_t image_crc32)
{
    boot_state_t state = {
        .generation = 1U,
        .confirmed_slot = confirmed_slot,
        .confirmed_version = confirmed_version,
        .pending_slot = confirmed_slot,
        .pending_version = confirmed_version,
        .pending_length = image_length,
        .pending_crc32 = image_crc32,
        .phase = BOOT_PHASE_CONFIRMED,
        .attempts_used = 0U
    };

    s_active_valid = false;
    return save_state(&state);
}

bool boot_state_set_pending(firmware_slot_t pending_slot,
                            uint32_t pending_version,
                            uint32_t image_length,
                            uint32_t image_crc32)
{
    boot_state_t state;

    if (!boot_state_load(&state)) {
        return false;
    }
    state.generation++;
    state.pending_slot = pending_slot;
    state.pending_version = pending_version;
    state.pending_length = image_length;
    state.pending_crc32 = image_crc32;
    state.phase = BOOT_PHASE_PENDING_TEST;
    state.attempts_used = 0U;
    return save_state(&state);
}

boot_attempt_result_t boot_state_consume_attempt(boot_state_t *state)
{
    boot_state_record_t record;
    uint32_t address;
    uint32_t new_mask;

    if ((state == NULL) || !latest_record(&record, &address) ||
        (record.phase != (uint8_t)BOOT_PHASE_PENDING_TEST)) {
        return BOOT_ATTEMPT_NOT_PENDING;
    }
    record_to_state(&record, state);
    s_active_address = address;
    s_active_valid = true;
    if (state->attempts_used >= 3U) {
        return BOOT_ATTEMPTS_EXHAUSTED;
    }
    new_mask = record.attempt_mask & ~(1U << state->attempts_used);
#ifndef BOOT_STATE_HOST_TEST
    fmc_unlock();
#endif
    if (!flash_program(address + BOOT_STATE_ATTEMPT_MASK_OFFSET, new_mask)) {
#ifndef BOOT_STATE_HOST_TEST
        fmc_lock();
#endif
        return BOOT_ATTEMPTS_EXHAUSTED;
    }
#ifndef BOOT_STATE_HOST_TEST
    fmc_lock();
#endif
    state->attempts_used++;
    return (boot_attempt_result_t)state->attempts_used;
}

bool boot_state_confirm(firmware_slot_t running_slot)
{
    boot_state_t state;

    if (!boot_state_load(&state)) {
        return false;
    }
    if (state.phase != BOOT_PHASE_PENDING_TEST) {
        return true;
    }
    if (state.pending_slot != running_slot) {
        return false;
    }
    state.generation++;
    state.confirmed_slot = running_slot;
    state.confirmed_version = state.pending_version;
    state.phase = BOOT_PHASE_CONFIRMED;
    state.attempts_used = 0U;
    return save_state(&state);
}

bool boot_state_rollback(void)
{
    boot_state_t state;

    if (!boot_state_load(&state)) {
        return false;
    }
    if (state.phase != BOOT_PHASE_PENDING_TEST) {
        return true;
    }
    state.generation++;
    state.pending_slot = state.confirmed_slot;
    state.pending_version = state.confirmed_version;
    state.phase = BOOT_PHASE_CONFIRMED;
    state.attempts_used = 0U;
    return save_state(&state);
}
