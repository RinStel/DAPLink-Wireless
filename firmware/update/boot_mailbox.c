#include "boot_mailbox.h"

#ifdef BOOT_MAILBOX_HOST_TEST
static uint16_t s_magic_low;
static uint16_t s_magic_high;
static uint16_t s_complement;

static void mailbox_write(uint32_t magic, uint16_t complement)
{
    s_magic_low = (uint16_t)magic;
    s_magic_high = (uint16_t)(magic >> 16);
    s_complement = complement;
}

static uint32_t mailbox_magic(void)
{
    return (uint32_t)s_magic_low | ((uint32_t)s_magic_high << 16);
}

static uint16_t mailbox_complement(void)
{
    return s_complement;
}

void boot_mailbox_test_reset(void)
{
    mailbox_write(0U, 0U);
}

void boot_mailbox_test_write(uint32_t magic, uint16_t complement)
{
    mailbox_write(magic, complement);
}
#else
#include "gd32f30x_bkp.h"
#include "gd32f30x_pmu.h"
#include "gd32f30x_rcu.h"

static void mailbox_enable(void)
{
    rcu_periph_clock_enable(RCU_BKPI);
    pmu_backup_write_enable();
}

static void mailbox_write(uint32_t magic, uint16_t complement)
{
    mailbox_enable();
    bkp_write_data(BKP_DATA_0, (uint16_t)magic);
    bkp_write_data(BKP_DATA_1, (uint16_t)(magic >> 16));
    bkp_write_data(BKP_DATA_2, complement);
    pmu_backup_write_disable();
}

static uint32_t mailbox_magic(void)
{
    return (uint32_t)bkp_read_data(BKP_DATA_0) |
           ((uint32_t)bkp_read_data(BKP_DATA_1) << 16);
}

static uint16_t mailbox_complement(void)
{
    return bkp_read_data(BKP_DATA_2);
}
#endif

void boot_mailbox_request_dfu(void)
{
    mailbox_write(BOOT_MAILBOX_MAGIC,
                  (uint16_t)~(uint16_t)BOOT_MAILBOX_MAGIC);
}

bool boot_mailbox_take_dfu_request(void)
{
    bool valid = (mailbox_magic() == BOOT_MAILBOX_MAGIC) &&
                 (mailbox_complement() ==
                  (uint16_t)~(uint16_t)BOOT_MAILBOX_MAGIC);
    if (valid) {
        mailbox_write(0U, 0U);
    }
    return valid;
}
