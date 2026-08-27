#include "dfu_flash.h"

#include <string.h>

#ifndef BOOT_STATE_HOST_TEST
#include "gd32f30x_fmc.h"
#endif

static dfu_flash_ops_t s_ops;

static uint32_t crc32_update_for_flash(uint32_t crc,
                                       const uint8_t *data,
                                       uint32_t length)
{
    uint32_t index;
    uint8_t bit;

    for (index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return crc;
}

static bool flash_read(uint32_t address, void *data, size_t length)
{
    if (s_ops.read != NULL) {
        return s_ops.read(address, data, length);
    }
#ifdef BOOT_STATE_HOST_TEST
    return false;
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
    return false;
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
    return false;
#else
    return fmc_word_program(address, value) == FMC_READY;
#endif
}

void dfu_flash_set_ops(const dfu_flash_ops_t *ops)
{
    if (ops == NULL) {
        memset(&s_ops, 0, sizeof(s_ops));
    } else {
        s_ops = *ops;
    }
}

dfu_flash_result_t dfu_flash_begin(dfu_flash_session_t *session,
                                   const firmware_image_header_t *header,
                                   firmware_slot_t active_slot,
                                   uint32_t confirmed_version,
                                   bool recovery_mode)
{
    firmware_image_result_t image_result;
    uint32_t page;
    uint32_t page_count;

    if ((session == NULL) || (header == NULL)) {
        return DFU_FLASH_ERR_NULL;
    }
    if (header->slot > (uint8_t)FIRMWARE_SLOT_B) {
        return DFU_FLASH_ERR_HEADER;
    }
    if ((active_slot <= FIRMWARE_SLOT_B) &&
        (header->slot == (uint8_t)active_slot)) {
        return DFU_FLASH_ERR_ACTIVE_SLOT;
    }
    image_result = firmware_image_header_validate(header,
                                                  (firmware_slot_t)header->slot,
                                                  confirmed_version,
                                                  recovery_mode);
    if (image_result == FIRMWARE_IMAGE_ERR_ADDRESS) {
        return DFU_FLASH_ERR_ADDRESS;
    }
    if (image_result != FIRMWARE_IMAGE_VALID) {
        return DFU_FLASH_ERR_HEADER;
    }
    page_count = (header->image_length + FIRMWARE_FLASH_PAGE_SIZE - 1U) /
                 FIRMWARE_FLASH_PAGE_SIZE;
    for (page = 0U; page < page_count; ++page) {
        uint32_t address = header->load_address +
                           page * FIRMWARE_FLASH_PAGE_SIZE;
#ifndef BOOT_STATE_HOST_TEST
        if (page == 0U) {
            fmc_unlock();
        }
#endif
        if (!flash_erase(address)) {
#ifndef BOOT_STATE_HOST_TEST
            fmc_lock();
#endif
            return DFU_FLASH_ERR_ERASE;
        }
    }
#ifndef BOOT_STATE_HOST_TEST
    fmc_lock();
#endif
    session->header = *header;
    session->active_slot = active_slot;
    session->next_offset = 0U;
    session->recovery_mode = recovery_mode;
    session->active = true;
    return DFU_FLASH_OK;
}

dfu_flash_result_t dfu_flash_write_block(dfu_flash_session_t *session,
                                         uint32_t offset,
                                         const uint8_t *data,
                                         size_t length)
{
    size_t index;

    if ((session == NULL) || (data == NULL) || !session->active) {
        return DFU_FLASH_ERR_NULL;
    }
    if ((offset != session->next_offset) || (length == 0U) ||
        ((length & 3U) != 0U) ||
        ((uint64_t)offset + length > session->header.image_length)) {
        return DFU_FLASH_ERR_SEQUENCE;
    }
#ifndef BOOT_STATE_HOST_TEST
    fmc_unlock();
#endif
    for (index = 0U; index < length; index += sizeof(uint32_t)) {
        uint32_t word;
        memcpy(&word, data + index, sizeof(word));
        if (!flash_program(session->header.load_address + offset +
                           (uint32_t)index, word)) {
#ifndef BOOT_STATE_HOST_TEST
            fmc_lock();
#endif
            session->active = false;
            return DFU_FLASH_ERR_PROGRAM;
        }
    }
#ifndef BOOT_STATE_HOST_TEST
    fmc_lock();
#endif
    session->next_offset += (uint32_t)length;
    return DFU_FLASH_OK;
}

dfu_flash_result_t dfu_flash_finish(dfu_flash_session_t *session)
{
    uint8_t buffer[64];
    uint32_t initial_msp;
    uint32_t reset_vector;
    uint32_t offset = 0U;
    uint32_t crc = 0xFFFFFFFFU;

    if ((session == NULL) || !session->active) {
        return DFU_FLASH_ERR_NULL;
    }
    if (session->next_offset != session->header.image_length) {
        return DFU_FLASH_ERR_SEQUENCE;
    }
    if ((session->header.image_length < 8U) ||
        !flash_read(session->header.load_address, &initial_msp,
                    sizeof(initial_msp)) ||
        !flash_read(session->header.load_address + sizeof(initial_msp),
                    &reset_vector, sizeof(reset_vector)) ||
        !firmware_image_vectors_validate(session->header.load_address,
                                         session->header.image_length,
                                         initial_msp, reset_vector)) {
        session->active = false;
        return DFU_FLASH_ERR_VECTOR;
    }
    while (offset < session->header.image_length) {
        uint32_t remaining = session->header.image_length - offset;
        uint32_t count = remaining > sizeof(buffer) ? sizeof(buffer) :
                         remaining;
        if (!flash_read(session->header.load_address + offset, buffer, count)) {
            session->active = false;
            return DFU_FLASH_ERR_READ;
        }
        crc = crc32_update_for_flash(crc, buffer, count);
        offset += count;
    }
    if ((~crc) != session->header.image_crc32) {
        session->active = false;
        return DFU_FLASH_ERR_CRC;
    }
    if (!boot_state_set_pending((firmware_slot_t)session->header.slot,
                                session->header.firmware_version_code,
                                session->header.image_length,
                                session->header.image_crc32)) {
        boot_state_t existing;

        /* A recovery device may have no valid journal at all.  The first
         * recovery image becomes the factory-confirmed image; an existing
         * journal must never be overwritten through this fallback. */
        if (!session->recovery_mode || boot_state_load(&existing) ||
            !boot_state_factory_init(
                (firmware_slot_t)session->header.slot,
                session->header.firmware_version_code,
                session->header.image_length,
                session->header.image_crc32)) {
            session->active = false;
            return DFU_FLASH_ERR_STATE_COMMIT;
        }
    }
    session->active = false;
    return DFU_FLASH_OK;
}

void dfu_flash_abort(dfu_flash_session_t *session)
{
    if (session != NULL) {
        session->active = false;
    }
}
