#include "boot_board.h"

#include "board_pins.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_misc.h"
#include "gd32f30x_rcu.h"

static volatile uint32_t s_millis;

/* Board LEDs are common-anode and therefore use active-low GPIO writes. */
static void output_write(uint32_t port, uint32_t pin, bool high)
{
    if (high) {
        gpio_bit_set(port, pin);
    } else {
        gpio_bit_reset(port, pin);
    }
}

void boot_board_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ,
              BOARD_LED_R_PIN | BOARD_LED_G_PIN | BOARD_LED_B_PIN);
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ,
              BOARD_USB_PULLUP_PIN);
    gpio_init(BOARD_KEY_PORT, GPIO_MODE_IPU, GPIO_OSPEED_2MHZ,
              BOARD_KEY_PIN);
    output_write(BOARD_LED_R_PORT, BOARD_LED_R_PIN, true);
    output_write(BOARD_LED_G_PORT, BOARD_LED_G_PIN, true);
    output_write(BOARD_LED_B_PORT, BOARD_LED_B_PIN, true);
    output_write(BOARD_USB_PULLUP_PORT, BOARD_USB_PULLUP_PIN, false);
    SysTick_Config(SystemCoreClock / 1000U);
}

uint32_t boot_board_millis(void)
{
    return s_millis;
}

void boot_board_systick_isr(void)
{
    ++s_millis;
}

void SysTick_Handler(void)
{
    boot_board_systick_isr();
}

bool boot_board_key_held(uint32_t stable_ms)
{
    uint32_t started;

    if (gpio_input_bit_get(BOARD_KEY_PORT, BOARD_KEY_PIN) != RESET) {
        return false;
    }
    started = boot_board_millis();
    while ((uint32_t)(boot_board_millis() - started) < stable_ms) {
        if (gpio_input_bit_get(BOARD_KEY_PORT, BOARD_KEY_PIN) != RESET) {
            return false;
        }
    }
    return true;
}

void boot_board_set_led(boot_led_rgb_t leds)
{
    output_write(BOARD_LED_R_PORT, BOARD_LED_R_PIN, !leds.red);
    output_write(BOARD_LED_G_PORT, BOARD_LED_G_PIN, !leds.green);
    output_write(BOARD_LED_B_PORT, BOARD_LED_B_PIN, !leds.blue);
}

void boot_board_usb_connect(bool connect)
{
    output_write(BOARD_USB_PULLUP_PORT, BOARD_USB_PULLUP_PIN, connect);
}

void boot_board_system_reset(void)
{
    NVIC_SystemReset();
}

void boot_board_jump_to_application(uint32_t vector_base)
{
    uint32_t application_msp = *(const uint32_t *)vector_base;
    void (*application_reset)(void) =
        (void (*)(void))(*(const uint32_t *)(vector_base + 4U));

    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    for (uint8_t index = 0U; index < 8U; ++index) {
        NVIC->ICER[index] = 0xFFFFFFFFU;
        NVIC->ICPR[index] = 0xFFFFFFFFU;
    }
    SCB->VTOR = vector_base;
    __DSB();
    __ISB();
    __set_MSP(application_msp);
    __enable_irq();
    application_reset();
}
