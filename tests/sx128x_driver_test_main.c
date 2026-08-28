/*
 * DAPLink-Wireless — SX128x 驱动测试主程序
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * 这个独立测试程序用于诊断 SX1281 初始化问题
 *
 * 编译此文件替代正常的 main.c，可以单独测试无线驱动
 */
#include "board.h"
#include "gd32f30x.h"

/* 外部诊断函数声明 */
extern void sx128x_init_diagnostic(void);

int main(void)
{
    /* 系统时钟和基本外设已经在 board_init() 中初始化 */

    /* 运行完整的 SX128x 诊断 */
    sx128x_init_diagnostic();

    /* 诊断函数包含无限循环，不会返回 */
    while (1) {
        /* 不应该到达这里 */
    }
}

/* 中断处理函数（最小实现） */
void SysTick_Handler(void)
{
    extern void board_systick_isr(void);
    board_systick_isr();
}

void NMI_Handler(void) {}
void HardFault_Handler(void)
{
    /* 硬件故障时红蓝灯快速交替 */
    while (1) {
        board_led_set(BOARD_LED_RED, true);
        board_led_set(BOARD_LED_BLUE, false);
        for (volatile uint32_t i = 0; i < 500000; i++);
        board_led_set(BOARD_LED_RED, false);
        board_led_set(BOARD_LED_BLUE, true);
        for (volatile uint32_t i = 0; i < 500000; i++);
    }
}
void MemManage_Handler(void) { while(1); }
void BusFault_Handler(void) { while(1); }
void UsageFault_Handler(void) { while(1); }
void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}
