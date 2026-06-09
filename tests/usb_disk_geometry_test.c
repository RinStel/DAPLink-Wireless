#include <assert.h>
#include <stdint.h>

#include "usb_disk_geometry.h"

#define TEST_DISK_SIZE   (32U * 512U)
#define TEST_BLOCK_SIZE  512U

int main(void)
{
    uint32_t byte_count = 0U;

    assert(usb_disk_byte_range_valid(TEST_DISK_SIZE, TEST_BLOCK_SIZE,
                                     0U, 1U, &byte_count));
    assert(byte_count == TEST_BLOCK_SIZE);
    assert(usb_disk_byte_range_valid(TEST_DISK_SIZE, TEST_BLOCK_SIZE,
                                     31U * TEST_BLOCK_SIZE, 1U,
                                     &byte_count));
    assert(!usb_disk_byte_range_valid(TEST_DISK_SIZE, TEST_BLOCK_SIZE,
                                      32U * TEST_BLOCK_SIZE, 1U,
                                      &byte_count));
    assert(!usb_disk_byte_range_valid(TEST_DISK_SIZE, TEST_BLOCK_SIZE,
                                      1U, 1U, &byte_count));
    assert(!usb_disk_byte_range_valid(TEST_DISK_SIZE, TEST_BLOCK_SIZE,
                                      31U * TEST_BLOCK_SIZE, 2U,
                                      &byte_count));
    assert(usb_disk_byte_range_valid(TEST_DISK_SIZE, TEST_BLOCK_SIZE,
                                     TEST_DISK_SIZE, 0U, &byte_count));
    assert(byte_count == 0U);
    return 0;
}
