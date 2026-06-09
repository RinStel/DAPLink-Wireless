#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "device_config.h"
#include "device_config_storage.h"

#define FLASH_BASE      0x08000000U
#define FLASH_SIZE      (256U * 1024U)
#define FLASH_PAGE_SIZE 2048U

static uint8_t s_flash[FLASH_SIZE];
static bool s_fail_erase;
static int32_t s_fail_program_at = -1;
static uint32_t s_program_calls;
static uint32_t s_erase_calls;

static bool flash_range(uint32_t address, size_t length, size_t *offset)
{
    if ((address < FLASH_BASE) ||
        ((uint64_t)address + length > (uint64_t)FLASH_BASE + FLASH_SIZE)) {
        return false;
    }
    *offset = address - FLASH_BASE;
    return true;
}

bool device_config_storage_test_read(uint32_t address,
                                     void *data, size_t length)
{
    size_t offset;

    if ((data == NULL) || !flash_range(address, length, &offset)) {
        return false;
    }
    memcpy(data, &s_flash[offset], length);
    return true;
}

bool device_config_storage_test_erase(uint32_t address)
{
    size_t offset;

    if (((address % FLASH_PAGE_SIZE) != 0U) ||
        !flash_range(address, FLASH_PAGE_SIZE, &offset)) {
        return false;
    }
    if (s_fail_erase) {
        return false;
    }
    memset(&s_flash[offset], 0xFF, FLASH_PAGE_SIZE);
    ++s_erase_calls;
    return true;
}

bool device_config_storage_test_program(uint32_t address, uint32_t value)
{
    size_t offset;
    uint32_t current;

    if (!flash_range(address, sizeof(value), &offset) ||
        ((address & 3U) != 0U)) {
        return false;
    }
    if ((s_fail_program_at >= 0) &&
        (s_program_calls == (uint32_t)s_fail_program_at)) {
        ++s_program_calls;
        return false;
    }
    memcpy(&current, &s_flash[offset], sizeof(current));
    if ((current & value) != value) {
        return false;
    }
    current &= value;
    memcpy(&s_flash[offset], &current, sizeof(current));
    ++s_program_calls;
    return true;
}

static void failure_clear(void)
{
    s_fail_program_at = -1;
    s_program_calls = 0U;
}

int main(void)
{
    device_config_t first;
    device_config_t second;
    device_config_t loaded;
    uint32_t erase_before;

    memset(s_flash, 0xFF, sizeof(s_flash));
    device_config_init();
    assert(!device_config_storage_load(&loaded));

    assert(device_config_apply("1234567890ABCDEF", DEVICE_MODE_WIRED,
                               DEVICE_RATE_AUTO,
                               SX128X_PROFILE_GFSK_1M));
    first = *device_config_get();
    assert(device_config_storage_save(&first));
    assert(device_config_storage_load(&loaded));
    assert(memcmp(&loaded, &first, sizeof(first)) == 0);

    erase_before = s_erase_calls;
    assert(device_config_storage_save(&first));
    assert(s_erase_calls == erase_before + 1U);
    erase_before = s_erase_calls;
    assert(device_config_storage_save(&first));
    assert(s_erase_calls == erase_before);

    assert(device_config_apply("FEDCBA0987654321",
                               DEVICE_MODE_WIRELESS_SLAVE,
                               DEVICE_RATE_FIXED,
                               SX128X_PROFILE_FLRC_650K));
    second = *device_config_get();

    s_fail_erase = true;
    assert(!device_config_storage_save(&second));
    s_fail_erase = false;
    assert(device_config_storage_load(&loaded));
    assert(memcmp(&loaded, &first, sizeof(first)) == 0);

    failure_clear();
    s_fail_program_at = 4;
    assert(!device_config_storage_save(&second));
    assert(device_config_storage_load(&loaded));
    assert(memcmp(&loaded, &first, sizeof(first)) == 0);

    failure_clear();
    s_fail_program_at = 10;
    assert(!device_config_storage_save(&second));
    assert(device_config_storage_load(&loaded));
    assert(memcmp(&loaded, &first, sizeof(first)) == 0);

    failure_clear();
    assert(device_config_storage_save(&second));
    assert(device_config_storage_load(&loaded));
    assert(memcmp(&loaded, &second, sizeof(second)) == 0);

    s_flash[FLASH_SIZE - 2U * FLASH_PAGE_SIZE + 12U] ^= 0x01U;
    assert(device_config_storage_load(&loaded));
    assert(memcmp(&loaded, &first, sizeof(first)) == 0);
    return 0;
}
