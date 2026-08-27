#include <assert.h>

#include "boot_mailbox.h"

int main(void)
{
    boot_mailbox_test_reset();
    assert(!boot_mailbox_take_dfu_request());
    boot_mailbox_request_dfu();
    assert(boot_mailbox_take_dfu_request());
    assert(!boot_mailbox_take_dfu_request());
    boot_mailbox_test_write(BOOT_MAILBOX_MAGIC ^ 1U,
                            (uint16_t)~BOOT_MAILBOX_MAGIC);
    assert(!boot_mailbox_take_dfu_request());
    return 0;
}
