#include "device_config_storage.h"

#include <stddef.h>
#include <string.h>

#ifdef DEVICE_CONFIG_STORAGE_HOST_TEST
#define FMC_SIZE 256U
extern bool device_config_storage_test_read(uint32_t address,
                                            void *data, size_t length);
extern bool device_config_storage_test_erase(uint32_t address);
extern bool device_config_storage_test_program(uint32_t address,
                                               uint32_t value);
#else
#include "gd32f30x_fmc.h"
#endif

#define CONFIG_FLASH_MAGIC       0x44415043U
#define CONFIG_FLASH_VERSION     1U
#define CONFIG_FLASH_PAGE_SIZE   2048U
#define CONFIG_FLASH_BASE        0x08000000U
#define CONFIG_COMMIT_MARKER     0x434F4D54U

#define RECORD_MAGIC_OFFSET      0U
#define RECORD_VERSION_OFFSET    4U
#define RECORD_SIZE_OFFSET       6U
#define RECORD_GENERATION_OFFSET 8U
#define RECORD_SYNC_OFFSET       12U
#define RECORD_RATE_OFFSET       29U
#define RECORD_PROFILE_OFFSET    30U
#define RECORD_MODE_OFFSET       31U
#define RECORD_CRC_OFFSET        36U
#define RECORD_COMMIT_OFFSET     40U
#define RECORD_SIZE              44U

typedef struct {
    uint32_t generation;
    char sync_code[DEVICE_SYNC_CODE_LENGTH + 1U];
    uint8_t device_mode;
    uint8_t rate_mode;
    uint8_t fixed_profile;
} persisted_config_t;

static uint16_t decode_u16_le(const uint8_t *input)
{
    return (uint16_t)input[0] | ((uint16_t)input[1] << 8);
}

static uint32_t decode_u32_le(const uint8_t *input)
{
    return (uint32_t)input[0] |
           ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) |
           ((uint32_t)input[3] << 24);
}

static void encode_u16_le(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void encode_u32_le(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static uint32_t crc32_calculate(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFU;
    size_t index;
    uint8_t bit;

    for (index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

static uint32_t config_slot_address(uint8_t slot)
{
    uint32_t flash_end;

    if ((slot >= 2U) || (FMC_SIZE < 4U)) {
        return 0U;
    }
    flash_end = CONFIG_FLASH_BASE + (uint32_t)FMC_SIZE * 1024U;
    return flash_end -
           (uint32_t)(2U - slot) * CONFIG_FLASH_PAGE_SIZE;
}

static bool flash_read(uint32_t address, void *data, size_t length)
{
#ifdef DEVICE_CONFIG_STORAGE_HOST_TEST
    return device_config_storage_test_read(address, data, length);
#else
    memcpy(data, (const void *)address, length);
    return true;
#endif
}

static bool flash_erase(uint32_t address)
{
#ifdef DEVICE_CONFIG_STORAGE_HOST_TEST
    return device_config_storage_test_erase(address);
#else
    return fmc_page_erase(address) == FMC_READY;
#endif
}

static bool flash_program(uint32_t address, uint32_t value)
{
#ifdef DEVICE_CONFIG_STORAGE_HOST_TEST
    return device_config_storage_test_program(address, value);
#else
    return fmc_word_program(address, value) == FMC_READY;
#endif
}

static void record_encode(uint8_t raw[RECORD_SIZE],
                          const persisted_config_t *record)
{
    memset(raw, 0xFF, RECORD_SIZE);
    encode_u32_le(&raw[RECORD_MAGIC_OFFSET], CONFIG_FLASH_MAGIC);
    encode_u16_le(&raw[RECORD_VERSION_OFFSET], CONFIG_FLASH_VERSION);
    encode_u16_le(&raw[RECORD_SIZE_OFFSET], RECORD_SIZE);
    encode_u32_le(&raw[RECORD_GENERATION_OFFSET], record->generation);
    memcpy(&raw[RECORD_SYNC_OFFSET], record->sync_code,
           sizeof(record->sync_code));
    raw[RECORD_RATE_OFFSET] = record->rate_mode;
    raw[RECORD_PROFILE_OFFSET] = record->fixed_profile;
    raw[RECORD_MODE_OFFSET] = record->device_mode;
    encode_u32_le(&raw[RECORD_CRC_OFFSET],
                  crc32_calculate(raw, RECORD_CRC_OFFSET));
}

static bool record_read(uint32_t address, persisted_config_t *record)
{
    uint8_t raw[RECORD_SIZE];

    if ((address == 0U) || (record == NULL)) {
        return false;
    }
    if (!flash_read(address, raw, sizeof(raw))) {
        return false;
    }
    if ((decode_u32_le(&raw[RECORD_MAGIC_OFFSET]) != CONFIG_FLASH_MAGIC) ||
        (decode_u16_le(&raw[RECORD_VERSION_OFFSET]) !=
         CONFIG_FLASH_VERSION) ||
        (decode_u16_le(&raw[RECORD_SIZE_OFFSET]) != RECORD_SIZE) ||
        (decode_u32_le(&raw[RECORD_COMMIT_OFFSET]) !=
         CONFIG_COMMIT_MARKER) ||
        (decode_u32_le(&raw[RECORD_CRC_OFFSET]) !=
         crc32_calculate(raw, RECORD_CRC_OFFSET))) {
        return false;
    }

    record->generation = decode_u32_le(&raw[RECORD_GENERATION_OFFSET]);
    memcpy(record->sync_code, &raw[RECORD_SYNC_OFFSET],
           sizeof(record->sync_code));
    record->rate_mode = raw[RECORD_RATE_OFFSET];
    record->fixed_profile = raw[RECORD_PROFILE_OFFSET];
    record->device_mode = raw[RECORD_MODE_OFFSET];
    if (record->device_mode > DEVICE_MODE_WIRELESS_SLAVE) {
        /* Records written before device modes existed were wireless peers. */
        record->device_mode = DEVICE_MODE_WIRELESS_HOST;
    }
    return true;
}

static bool config_equals_record(const device_config_t *config,
                                 const persisted_config_t *record)
{
    return (memcmp(config->sync_code, record->sync_code,
                   sizeof(record->sync_code)) == 0) &&
           ((uint8_t)config->rate_mode == record->rate_mode) &&
           ((uint8_t)config->fixed_profile == record->fixed_profile) &&
           ((uint8_t)config->device_mode == record->device_mode);
}

static bool latest_record(persisted_config_t *record, uint32_t *address,
                          uint8_t *valid_count)
{
    persisted_config_t slot0;
    persisted_config_t slot1;
    uint32_t slot0_address = config_slot_address(0U);
    uint32_t slot1_address = config_slot_address(1U);
    bool valid0 = record_read(slot0_address, &slot0);
    bool valid1 = record_read(slot1_address, &slot1);

    if (valid_count != NULL) {
        *valid_count = (uint8_t)valid0 + (uint8_t)valid1;
    }
    if (!valid0 && !valid1) {
        return false;
    }
    if (valid1 && (!valid0 ||
        ((int32_t)(slot1.generation - slot0.generation) > 0))) {
        *record = slot1;
        *address = slot1_address;
    } else {
        *record = slot0;
        *address = slot0_address;
    }
    return true;
}

bool device_config_storage_load(device_config_t *config)
{
    persisted_config_t record;
    uint32_t address;

    if ((config == NULL) || !latest_record(&record, &address, NULL)) {
        return false;
    }
    (void)address;
    if (!device_config_apply(record.sync_code,
                             (device_mode_t)record.device_mode,
                             (device_rate_mode_t)record.rate_mode,
                             (sx128x_profile_t)record.fixed_profile)) {
        return false;
    }
    *config = *device_config_get();
    return true;
}

bool device_config_storage_matches(const device_config_t *config)
{
    persisted_config_t record;
    uint32_t address;

    return (config != NULL) &&
           latest_record(&record, &address, NULL) &&
           config_equals_record(config, &record);
}

bool device_config_storage_save(const device_config_t *config)
{
    persisted_config_t record;
    persisted_config_t current;
    uint8_t raw[RECORD_SIZE];
    uint32_t current_address;
    uint32_t target_address;
    uint32_t word;
    uint8_t valid_count;
    size_t offset;

    if (!device_config_is_valid(config)) {
        return false;
    }
    if (latest_record(&current, &current_address, &valid_count)) {
        if (config_equals_record(config, &current) &&
            (valid_count == 2U)) {
            return true;
        }
        target_address = current_address == config_slot_address(0U)
                             ? config_slot_address(1U)
                             : config_slot_address(0U);
    } else {
        memset(&current, 0, sizeof(current));
        target_address = config_slot_address(0U);
    }

    record.generation = current.generation + 1U;
    memcpy(record.sync_code, config->sync_code, sizeof(record.sync_code));
    record.device_mode = (uint8_t)config->device_mode;
    record.rate_mode = (uint8_t)config->rate_mode;
    record.fixed_profile = (uint8_t)config->fixed_profile;
    record_encode(raw, &record);

#ifndef DEVICE_CONFIG_STORAGE_HOST_TEST
    fmc_unlock();
    fmc_flag_clear(FMC_FLAG_BANK0_END |
                   FMC_FLAG_BANK0_WPERR |
                   FMC_FLAG_BANK0_PGERR);
#endif
    if ((target_address == 0U) || !flash_erase(target_address)) {
#ifndef DEVICE_CONFIG_STORAGE_HOST_TEST
        fmc_lock();
#endif
        return false;
    }

    for (offset = 0U; offset < RECORD_COMMIT_OFFSET;
        offset += sizeof(uint32_t)) {
        word = decode_u32_le(&raw[offset]);
        if (!flash_program(target_address + (uint32_t)offset, word)) {
#ifndef DEVICE_CONFIG_STORAGE_HOST_TEST
            fmc_lock();
#endif
            return false;
        }
    }
    if (!flash_program(target_address + RECORD_COMMIT_OFFSET,
                       CONFIG_COMMIT_MARKER)) {
#ifndef DEVICE_CONFIG_STORAGE_HOST_TEST
        fmc_lock();
#endif
        return false;
    }
#ifndef DEVICE_CONFIG_STORAGE_HOST_TEST
    fmc_lock();
#endif
    return record_read(target_address, &record) &&
           config_equals_record(config, &record);
}
