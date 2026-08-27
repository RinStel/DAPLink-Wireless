#ifndef DFU_FLASH_H
#define DFU_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_state.h"
#include "firmware_image.h"

typedef enum {
    DFU_FLASH_OK = 0U,
    DFU_FLASH_ERR_NULL,
    DFU_FLASH_ERR_ACTIVE_SLOT,
    DFU_FLASH_ERR_ADDRESS,
    DFU_FLASH_ERR_HEADER,
    DFU_FLASH_ERR_ERASE,
    DFU_FLASH_ERR_SEQUENCE,
    DFU_FLASH_ERR_PROGRAM,
    DFU_FLASH_ERR_READ,
    DFU_FLASH_ERR_CRC,
    DFU_FLASH_ERR_VECTOR,
    DFU_FLASH_ERR_STATE_COMMIT
} dfu_flash_result_t;

typedef struct {
    bool (*read)(uint32_t address, void *data, size_t length);
    bool (*erase_page)(uint32_t address);
    bool (*program_word)(uint32_t address, uint32_t value);
} dfu_flash_ops_t;

typedef struct {
    firmware_image_header_t header;
    firmware_slot_t active_slot;
    uint32_t next_offset;
    bool recovery_mode;
    bool active;
} dfu_flash_session_t;

void dfu_flash_set_ops(const dfu_flash_ops_t *ops);
dfu_flash_result_t dfu_flash_begin(dfu_flash_session_t *session,
                                   const firmware_image_header_t *header,
                                   firmware_slot_t active_slot,
                                   uint32_t confirmed_version,
                                   bool recovery_mode);
dfu_flash_result_t dfu_flash_write_block(dfu_flash_session_t *session,
                                         uint32_t offset,
                                         const uint8_t *data,
                                         size_t length);
dfu_flash_result_t dfu_flash_finish(dfu_flash_session_t *session);
void dfu_flash_abort(dfu_flash_session_t *session);

#endif
