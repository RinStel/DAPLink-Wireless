# U5 GPIO Mapping and USB Power Input Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Synchronize the firmware board abstraction with the confirmed U5 schematic connections, especially the input-only `USB_AUTO_EN` signal, without driving unrelated USB or target-reset nets.

**Architecture:** Keep all GD32F303CCT6 board mappings in `firmware/bsp/board_pins.h`. Configure `PA0/USB_AUTO_EN` as an external input and expose only a read API; remove the obsolete independent target-power output APIs because both `SY6280AAAC` enable pins and `Q2.G` share that hardware signal. Make the USB stack consume the same board mapping and verify the mapping with a host-compiled regression test.

**Tech Stack:** C11 host tests, PowerShell test harness, GD32F30x standard peripheral library, GCC firmware build, Markdown project manuals.

## Global Constraints

- `USB_AUTO_EN` is an external input to the MCU representing USB power validity; the firmware must never configure or drive `PA0` as an output.
- `U_TGT_SW1.EN`, `U_TGT_SW2.EN`, and `Q2.G` share `USB_AUTO_EN`; the `SY6280AAAC` enable input is high-active.
- Preserve the existing radio protocol v1 and all unrelated user changes; do not edit the EasyEDA schematic or push/commit without an explicit request.
- Use the confirmed U5 mapping in `docs/schematic_u5_connections.md` as the hardware source of truth.
- Verification must include the board-pin host regression, all host tests, and the GCC Debug/Release firmware builds.

---

### Task 1: Add a failing board-mapping regression

**Files:**
- Create: `tests/board_pins_test.c`
- Modify: `scripts/test_host.ps1`

**Interfaces:**
- Consumes: current `firmware/bsp/board_pins.h` and `firmware/usb/usbd_conf.h` macro interfaces.
- Produces: a `board-pins` host test that fails against the old mappings and protects the confirmed U5 pin contract.

- [x] **Step 1: Write the failing test and register it.**

Add a host test that includes `board_pins.h` and `usbd_conf.h`, then uses compile-time assertions for the confirmed ports/pins:

```c
#include "board_pins.h"
#include "usbd_conf.h"

_Static_assert(BOARD_LED_R_PORT == GPIOC, "LED R port changed");
_Static_assert(BOARD_LED_R_PIN == GPIO_PIN_13, "LED R pin changed");
_Static_assert(BOARD_LED_G_PIN == GPIO_PIN_14, "LED G pin must be PC14");
_Static_assert(BOARD_LED_B_PIN == GPIO_PIN_15, "LED B pin must be PC15");
_Static_assert(BOARD_USB_AUTO_EN_PORT == GPIOA, "USB_AUTO_EN must be PA0");
_Static_assert(BOARD_USB_AUTO_EN_PIN == GPIO_PIN_0, "USB_AUTO_EN must be PA0");
_Static_assert(BOARD_USB_PULLUP_PORT == GPIOA, "USB pull-up must be PA8");
_Static_assert(BOARD_USB_PULLUP_PIN == GPIO_PIN_8, "USB pull-up must be PA8");
_Static_assert(BOARD_RF_RX_EN_PORT == GPIOA, "RF RX_EN must be PA1");
_Static_assert(BOARD_RF_RX_EN_PIN == GPIO_PIN_1, "RF RX_EN must be PA1");
_Static_assert(BOARD_RF_TX_EN_PORT == GPIOA, "RF TX_EN must be PA2");
_Static_assert(BOARD_RF_TX_EN_PIN == GPIO_PIN_2, "RF TX_EN must be PA2");
_Static_assert(BOARD_RF_NRESET_PORT == GPIOA, "RF NRESET must be PA3");
_Static_assert(BOARD_RF_NRESET_PIN == GPIO_PIN_3, "RF NRESET must be PA3");
_Static_assert(BOARD_RF_BUSY_PORT == GPIOB, "RF BUSY must be PB1");
_Static_assert(BOARD_RF_BUSY_PIN == GPIO_PIN_1, "RF BUSY must be PB1");
_Static_assert(BOARD_RF_DIO1_PORT == GPIOB, "RF DIO1 must be PB5");
_Static_assert(BOARD_RF_DIO1_PIN == GPIO_PIN_5, "RF DIO1 must be PB5");
_Static_assert(BOARD_TGT_NRST_PIN == GPIO_PIN_15, "target NRST must be PB15");
_Static_assert(BOARD_TGT_BOOT_PIN == GPIO_PIN_14, "target BOOT must be PB14");
_Static_assert(BOARD_TGT_SWCLK_PIN == GPIO_PIN_13, "target SWCLK must be PB13");
_Static_assert(BOARD_TGT_SWDIO_PIN == GPIO_PIN_12, "target SWDIO must be PB12");
_Static_assert(USB_PULLUP == BOARD_USB_PULLUP_PORT, "USB port mapping diverged");
_Static_assert(USB_PULLUP_PIN == BOARD_USB_PULLUP_PIN, "USB pin mapping diverged");

#ifdef BOARD_TGT_5V_EN_PORT
#error "independent target 5V output must be removed"
#endif

#ifdef BOARD_TGT_3V3_EN_PORT
#error "independent target 3V3 output must be removed"
#endif

int main(void)
{
    return 0;
}
```

Register `board-pins` in `scripts/test_host.ps1` with `GD32F30X_HD`, the existing GD32 system include directories, and `tests/board_pins_test.c` as its only source.

- [x] **Step 2: Run the new test to verify it fails.**

Run:

```powershell
.\scripts\test_host.ps1 -Name board-pins
```

Expected: compilation fails because the current board macros still contain the old LED, USB, RF, target SWD, and independent target-power mappings.

### Task 2: Apply the confirmed board and USB mapping

**Files:**
- Modify: `firmware/bsp/board_pins.h`
- Modify: `firmware/bsp/board.h`
- Modify: `firmware/bsp/board.c`
- Modify: `firmware/usb/usbd_conf.h`

**Interfaces:**
- Consumes: the failing `board-pins` regression from Task 1.
- Produces: `bool board_usb_power_present(void)` for reading `USB_AUTO_EN`; no independent target-power output API.

- [x] **Step 1: Replace the board mapping macros.**

Update `board_pins.h` to use these exact mappings:

```c
#define BOARD_LED_R_PORT       GPIOC
#define BOARD_LED_R_PIN        GPIO_PIN_13
#define BOARD_LED_G_PORT       GPIOC
#define BOARD_LED_G_PIN        GPIO_PIN_14
#define BOARD_LED_B_PORT       GPIOC
#define BOARD_LED_B_PIN        GPIO_PIN_15

#define BOARD_USB_AUTO_EN_PORT GPIOA
#define BOARD_USB_AUTO_EN_PIN  GPIO_PIN_0
#define BOARD_USB_PULLUP_PORT  GPIOA
#define BOARD_USB_PULLUP_PIN   GPIO_PIN_8

#define BOARD_RF_NSS_PORT      GPIOA
#define BOARD_RF_NSS_PIN       GPIO_PIN_4
#define BOARD_RF_RX_EN_PORT    GPIOA
#define BOARD_RF_RX_EN_PIN     GPIO_PIN_1
#define BOARD_RF_TX_EN_PORT    GPIOA
#define BOARD_RF_TX_EN_PIN     GPIO_PIN_2
#define BOARD_RF_NRESET_PORT   GPIOA
#define BOARD_RF_NRESET_PIN    GPIO_PIN_3
#define BOARD_RF_BUSY_PORT     GPIOB
#define BOARD_RF_BUSY_PIN      GPIO_PIN_1
#define BOARD_RF_DIO1_PORT     GPIOB
#define BOARD_RF_DIO1_PIN      GPIO_PIN_5

#define BOARD_TGT_NRST_PORT    GPIOB
#define BOARD_TGT_NRST_PIN     GPIO_PIN_15
#define BOARD_TGT_BOOT_PORT    GPIOB
#define BOARD_TGT_BOOT_PIN     GPIO_PIN_14
#define BOARD_TGT_SWCLK_PORT   GPIOB
#define BOARD_TGT_SWCLK_PIN    GPIO_PIN_13
#define BOARD_TGT_SWDIO_PORT   GPIOB
#define BOARD_TGT_SWDIO_PIN    GPIO_PIN_12
```

Remove `BOARD_TGT_5V_EN_*` and `BOARD_TGT_3V3_EN_*`, and make `board_pins.h` include the GD32 GPIO definitions it directly uses.

- [x] **Step 2: Configure PA0 as an input and remove stale output control.**

In `board.h`, replace the two target-power prototypes with:

```c
bool board_usb_power_present(void);
```

In `board.c`, remove the two target-power safe writes and output-mask entries. Add `BOARD_USB_AUTO_EN` to the input initialization as `GPIO_MODE_IN_FLOATING`, keep `BOARD_USB_PULLUP` as an output, and implement:

```c
bool board_usb_power_present(void)
{
    return gpio_input_bit_get(BOARD_USB_AUTO_EN_PORT,
                               BOARD_USB_AUTO_EN_PIN) != RESET;
}
```

Remove the implementations of `board_target_5v_enable()` and `board_target_3v3_enable()`. Keep the existing software USB D+ pull-up API on `PA8`.

- [x] **Step 3: Make the USB configuration consume the board mapping.**

Include `board_pins.h` from `firmware/usb/usbd_conf.h` and define:

```c
#define USB_PULLUP     BOARD_USB_PULLUP_PORT
#define USB_PULLUP_PIN BOARD_USB_PULLUP_PIN
```

Do not retain a second hard-coded `GPIOB/GPIO_PIN_8` definition.

- [x] **Step 4: Run the focused regression.**

Run:

```powershell
.\scripts\test_host.ps1 -Name board-pins
```

Expected: `Board pin mapping tests passed`.

### Task 3: Verify driver integration without adding unconfirmed behavior

**Files:**
- Inspect: `firmware/drivers/radio/radio_hal.c`
- Inspect: `firmware/drivers/swd/target_swd.c`
- Modify only if compilation exposes a direct old-pin dependency.

**Interfaces:**
- Consumes: the corrected `BOARD_RF_*` and `BOARD_TGT_*` macros.
- Produces: radio and target SWD code that uses the corrected board abstraction; no speculative DIO2/DIO3 or SW1 feature.

- [x] **Step 1: Search for stale pin definitions.**

Run:

```powershell
rg -n "GPIO_PIN_(0|1|2|3|4|5|6|7|8|10|11|12|13|14|15)|BOARD_(TGT_5V|TGT_3V3)_EN|BOARD_RF_(RX_EN|TX_EN|NRESET|BUSY|DIO1)|BOARD_TGT_(NRST|BOOT|SWCLK|SWDIO)" firmware
```

Expected: radio and SWD operations use board macros; the only intentional raw RF SPI pins remain `PA5/PA6/PA7`, which match the schematic.

- [x] **Step 2: Build the firmware targets.**

Run:

```powershell
.\\scripts\\build_gcc.ps1 -Configuration Debug
.\\scripts\\build_gcc.ps1 -Configuration Release
```

Expected: both configurations compile without references to removed target-power APIs.

### Task 4: Update the hardware record with the confirmed USB_AUTO_EN semantics

**Files:**
- Modify: `docs/schematic_u5_connections.md`
- Modify: `docs/hardware_manual.md`

**Interfaces:**
- Consumes: EasyEDA bridge confirmation and the implemented board API.
- Produces: documentation that states `USB_AUTO_EN` is an external, high-active hardware signal observed by PA0 and no longer lists it as unresolved.

- [x] **Step 1: Replace the stale pending wording.**

Record that `PA0` is an input, `U_TGT_SW1.EN`, `U_TGT_SW2.EN`, and `Q2.G` share the net, and the SY6280 enable is high-active. State that target power is hardware-controlled by USB power validity and has no independent MCU 5 V/3.3 V enable API.

- [x] **Step 2: Run the Chinese technical-text and link checks.**

Run:

```powershell
python C:\\Users\\YSCha\\.codex\\skills\\cste-zh\\scripts\\cste_lint.py --help
```

Then run the linter command selected from its displayed file arguments for `README.md`, `CHANGELOG.md`, and the project-owned `docs/*.md`; also run the repository’s Markdown link resolver used by the previous manual-consolidation verification. Expected: all project manuals pass with no broken links or ambiguous requirement wording.

### Task 5: Run the complete verification set

**Files:**
- Inspect: generated test/build output only.

- [x] **Step 1: Run all host tests.**

```powershell
.\scripts\test_host.ps1 -Name all
```

Expected: all existing suites plus `Board pin mapping tests passed` succeed.

- [x] **Step 2: Run release verification without Keil.**

```powershell
.\scripts\verify_release.ps1 -SkipKeil
```

Expected: source/repository checks, host tests, GCC Debug/Release, and reproducibility verification pass; the existing radio protocol v1 manifest remains unchanged.

- [x] **Step 3: Review the final diff and working tree.**

```powershell
git diff --check
git status --short --branch
git diff --stat
```

Expected: only the planned firmware, test, script, and hardware-manual files are changed. Do not commit or push unless the user asks.
