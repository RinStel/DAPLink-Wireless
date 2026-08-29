#include <assert.h>

#include "target_swd.h"

#ifndef TARGET_SWD_CLOCK_IDLE_HIGH
#define TARGET_SWD_CLOCK_IDLE_HIGH 1U
#endif

#ifndef TARGET_SWD_SAMPLE_CLOCK_HIGH
#define TARGET_SWD_SAMPLE_CLOCK_HIGH 0U
#endif

/* 纯函数回归：验证时钟限制和 PB12 寄存器字段保护。 */
int main(void)
{
    uint32_t initial_ctl1 = (0xAU << 20) | (0x5U << 16) | 0x1234U;
    uint32_t updated_ctl1 = target_swd_swdio_ctl1_set_mode(
        initial_ctl1, 0x48U);

    assert(target_swd_normalize_clock(0U) == 100000U);
    assert(target_swd_normalize_clock(1000U) == 10000U);
    assert(target_swd_normalize_clock(2000000U) == 2000000U);
    assert(target_swd_normalize_clock(4000000U) == 4000000U);
    assert(target_swd_normalize_clock(10000000U) == 4000000U);
    assert((updated_ctl1 & TARGET_SWDIO_CTL_MASK) == (0x8U << 16));
    assert((updated_ctl1 & (0xFU << 20)) == (0xAU << 20));
    assert((updated_ctl1 & 0xFFFFU) == (initial_ctl1 & 0xFFFFU));
    assert(TARGET_SWD_CLOCK_IDLE_HIGH == 1U);
    assert(TARGET_SWD_SAMPLE_CLOCK_HIGH == 0U);
    return 0;
}
