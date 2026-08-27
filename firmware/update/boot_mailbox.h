#ifndef BOOT_MAILBOX_H
#define BOOT_MAILBOX_H

#include <stdbool.h>
#include <stdint.h>

#define BOOT_MAILBOX_MAGIC 0x44465531U

void boot_mailbox_request_dfu(void);
bool boot_mailbox_take_dfu_request(void);

#ifdef BOOT_MAILBOX_HOST_TEST
void boot_mailbox_test_reset(void);
void boot_mailbox_test_write(uint32_t magic, uint16_t complement);
#endif

#endif
