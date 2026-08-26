# USB DFU 与 A/B 固件回退实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 GD32F303CCT6 增加通过 MSC 配置盘进入的 USB DFU、A/B 双镜像试运行确认、三次失败自动回退、恢复按键和 `STAT_LED` 状态提示。

**Architecture:** 16 KiB Bootloader 固定在 `0x08000000`，两个 116 KiB 应用槽分别从 `0x08004000` 和 `0x08021000` 执行。主机工具修改 `CONFIG.TXT` 触发软件复位，再使用标准 DFU 1.1 下载 `.dwup` 中的非活动槽镜像；Bootloader 验证并原子提交候选状态，成功后自动复位试运行。

**Tech Stack:** C11、Cortex-M4/GD32F303、GD32F30x 标准外设库、GD32 USB Device 库、GNU Arm Embedded Toolchain、PowerShell、Python 3 标准库、`dfu-util`、主机 GCC 回归测试。

**Spec:** `docs/superpowers/specs/2026-08-26-usb-dfu-design.md`

## Global Constraints

- Bootloader 必须位于 `0x08000000–0x08003FFF`，且不得超过 16 KiB。
- Slot A 必须位于 `0x08004000–0x08020FFF`，且不得超过 116 KiB。
- Slot B 必须位于 `0x08021000–0x0803DFFF`，且不得超过 116 KiB。
- Boot 状态必须位于 `0x0803E000–0x0803EFFF`。
- 设备配置必须保留在 `0x0803F000–0x0803FFFF`。
- 正常 DFU 只允许 `firmware_version_code` 严格递增；按键恢复 DFU 允许同版本和降级。
- 两种 DFU 模式都必须检查 GD32F303CC、槽、加载地址、长度、向量表、头部 CRC32 和镜像 CRC32。
- DFU 不得擦写 Bootloader、活动槽、Boot 状态或设备配置区域。
- DFU 等待和错误状态不设空闲超时；成功 Manifest 完成后必须自动复位。
- DFU `UPLOAD` 必须禁用。
- 应用 USB PID 保持 `0x1290`；Bootloader DFU PID 使用 `0x1291`。
- 应用 USB 复合描述符不得增加 DFU Runtime 接口。
- 不得修改 `vendor/` 中的第三方源码。
- 每个任务只提交当前任务的 `Files` 列表中的文件。不得提交工作区中的既有无关修改。

---

## 文件结构

- `firmware/update/firmware_layout.h`：唯一的 Flash 地址、页大小、槽大小和 MCU 标识。
- `firmware/update/firmware_image.h/.c`：镜像头、CRC32、范围和向量表验证。
- `firmware/update/boot_state.h/.c`：双页 Boot 状态、候选提交、尝试标记、确认和回退。
- `firmware/update/boot_mailbox.h/.c`：应用与 Bootloader 之间的 BKP 一次性 DFU magic。
- `firmware/bootloader/boot_policy.h/.c`：无硬件依赖的启动决策。
- `firmware/bootloader/boot_board.h/.c`：最小按键、LED、时钟、复位和应用跳转。
- `firmware/bootloader/boot_led.h/.c`：DFU 与回退状态到逻辑 RGB 的映射。
- `firmware/bootloader/dfu_flash.h/.c`：只写非活动槽的 Flash 事务。
- `firmware/bootloader/dfu_device.h/.c`：独立 DFU 描述符和 DFU 1.1 状态机。
- `firmware/bootloader/main.c`：启动决策、DFU 主循环和成功复位。
- `firmware/linker/gd32f303xC_bootloader.ld`、`gd32f303xC_slot_a.ld`、`gd32f303xC_slot_b.ld`：三个链接布局。
- `tools/dwup_format.py`、`tools/build_dwup.py`：升级包格式和生成器。
- `tools/daplink_updater.py`：MSC 触发、镜像选择和 `dfu-util` 调用。

---

### Task 1: 固化 Flash 布局、镜像头和版本代码

**Files:**
- Create: `firmware/update/firmware_layout.h`
- Create: `firmware/update/firmware_image.h`
- Create: `firmware/update/firmware_image.c`
- Create: `tests/firmware_image_test.c`
- Modify: `firmware/app/firmware_version.h`
- Modify: `scripts/test_host.ps1`

**Interfaces:**
- Produces: `firmware_slot_t`、`firmware_image_header_t`、`firmware_crc32()`、`firmware_image_header_seal()`、`firmware_image_header_validate()`、`firmware_image_vectors_validate()`。
- Consumes: 无。

- [ ] **Step 1: 写入失败的镜像验证测试**

```c
firmware_image_header_t header = {
    .magic = FIRMWARE_IMAGE_MAGIC,
    .format_version = FIRMWARE_IMAGE_FORMAT_VERSION,
    .mcu_id = FIRMWARE_MCU_GD32F303CC,
    .slot = FIRMWARE_SLOT_B,
    .load_address = FIRMWARE_SLOT_B_BASE,
    .image_length = 47512U,
    .firmware_version_code = 800U,
};
firmware_image_header_seal(&header);
assert(firmware_image_header_validate(&header, FIRMWARE_SLOT_B,
                                      799U, false) ==
       FIRMWARE_IMAGE_VALID);
header.image_length = FIRMWARE_SLOT_SIZE + 1U;
assert(firmware_image_header_validate(&header, FIRMWARE_SLOT_B,
                                      799U, false) ==
       FIRMWARE_IMAGE_ERR_LENGTH);
```

测试必须覆盖错误 magic、格式、MCU、槽、地址、同版本和降级。测试还必须覆盖恢复模式的版本规则、头 CRC、地址加长度回绕、MSP 越界、非 Thumb 复位向量和复位向量越界。

- [ ] **Step 2: 注册并运行测试，确认失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name firmware-image`

Expected: FAIL，错误包含 `firmware_image.h: No such file or directory`。

- [ ] **Step 3: 定义唯一布局常量和固定 64 字节镜像头**

```c
#define FIRMWARE_BOOT_BASE        0x08000000U
#define FIRMWARE_BOOT_SIZE        0x00004000U
#define FIRMWARE_SLOT_A_BASE      0x08004000U
#define FIRMWARE_SLOT_B_BASE      0x08021000U
#define FIRMWARE_SLOT_SIZE        0x0001D000U
#define FIRMWARE_BOOT_STATE_BASE  0x0803E000U
#define FIRMWARE_CONFIG_BASE      0x0803F000U
#define FIRMWARE_FLASH_END        0x08040000U
#define FIRMWARE_FLASH_PAGE_SIZE  0x00000800U
#define FIRMWARE_SRAM_BASE        0x20000000U
#define FIRMWARE_SRAM_END         0x2000C000U
```

使用固定宽度整数和显式保留字节；用 `_Static_assert(sizeof(firmware_image_header_t) == 64U, "firmware image header must be 64 bytes")` 固定 ABI。所有地址加长度计算先提升到 `uint64_t`。

- [ ] **Step 4: 实施 CRC32、头部、版本和向量表验证**

```c
firmware_image_result_t firmware_image_header_validate(
    const firmware_image_header_t *header,
    firmware_slot_t expected_slot,
    uint32_t confirmed_version,
    bool recovery_mode);

bool firmware_image_vectors_validate(uint32_t slot_base,
                                     uint32_t image_length,
                                     uint32_t initial_msp,
                                     uint32_t reset_vector);
```

正常模式必须满足 `candidate > confirmed`；恢复模式只跳过版本比较。

- [ ] **Step 5: 增加数值版本并运行测试**

在 `firmware_version.h` 增加 `#define FIRMWARE_VERSION_CODE 800U`。

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name firmware-image`

Expected: `FIRMWARE_IMAGE_TEST=PASS`。

- [ ] **Step 6: 提交**

```powershell
git add firmware/update/firmware_layout.h firmware/update/firmware_image.h firmware/update/firmware_image.c firmware/app/firmware_version.h tests/firmware_image_test.c scripts/test_host.ps1
git commit -m "feat(update): define image layout and validation"
```

---

### Task 2: 实施掉电安全的 Boot 状态日志

**Files:**
- Create: `firmware/update/boot_state.h`
- Create: `firmware/update/boot_state.c`
- Create: `tests/boot_state_test.c`
- Modify: `scripts/test_host.ps1`

**Interfaces:**
- Consumes: `firmware_slot_t`、布局常量和 `firmware_crc32()`。
- Produces: `boot_state_load()`、`boot_state_factory_init()`、`boot_state_set_pending()`、`boot_state_consume_attempt()`、`boot_state_confirm(firmware_slot_t running_slot)`、`boot_state_rollback()`。

- [ ] **Step 1: 写入双页原子性和三次尝试测试**

```c
boot_state_t state;
flash_reset();
assert(!boot_state_load(&state));
assert(boot_state_factory_init(FIRMWARE_SLOT_A, 799U,
                               47512U, 0x12345678U));
assert(boot_state_set_pending(FIRMWARE_SLOT_B, 800U,
                              48000U, 0x87654321U));
assert(boot_state_consume_attempt(&state) == BOOT_ATTEMPT_1);
assert(boot_state_consume_attempt(&state) == BOOT_ATTEMPT_2);
assert(boot_state_consume_attempt(&state) == BOOT_ATTEMPT_3);
assert(boot_state_consume_attempt(&state) == BOOT_ATTEMPTS_EXHAUSTED);
```

对每个擦除和编程调用注入失败。每次失败后重新加载，结果只能是旧的已确认状态或完整的 `PENDING_TEST`。

- [ ] **Step 2: 注册并运行测试，确认失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name boot-state`

Expected: FAIL，错误包含未定义的 `boot_state_load`。

- [ ] **Step 3: 定义状态记录和可注入 Flash 操作**

```c
typedef struct {
    uint32_t generation;
    firmware_slot_t confirmed_slot;
    uint32_t confirmed_version;
    firmware_slot_t pending_slot;
    uint32_t pending_version;
    uint32_t pending_length;
    uint32_t pending_crc32;
    boot_phase_t phase;
} boot_state_t;

typedef struct {
    bool (*read)(uint32_t, void *, size_t);
    bool (*erase_page)(uint32_t);
    bool (*program_word)(uint32_t, uint32_t);
} boot_state_flash_ops_t;
```

- [ ] **Step 4: 实施双页提交和尝试位图**

新页按“擦除、记录主体、回读 CRC、最后写提交字”执行。三个尝试字分别从 `0xFFFFFFFF` 写为 `0x00000000`，不得重复编程同一字。

- [ ] **Step 5: 运行故障注入测试**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name boot-state`

Expected: `BOOT_STATE_TEST=PASS`，退出码为 0。

- [ ] **Step 6: 提交**

```powershell
git add firmware/update/boot_state.h firmware/update/boot_state.c tests/boot_state_test.c scripts/test_host.ps1
git commit -m "feat(update): add atomic boot state journal"
```

---

### Task 3: 实施启动策略、BKP mailbox 和应用确认契约

**Files:**
- Create: `firmware/bootloader/boot_policy.h`
- Create: `firmware/bootloader/boot_policy.c`
- Create: `firmware/update/boot_mailbox.h`
- Create: `firmware/update/boot_mailbox.c`
- Create: `tests/boot_policy_test.c`
- Create: `tests/boot_mailbox_test.c`
- Modify: `scripts/test_host.ps1`

**Interfaces:**
- Consumes: `boot_state_t`、镜像有效性、复位原因和按键标志。
- Produces: `boot_policy_decide()`、`boot_mailbox_request_dfu()`、`boot_mailbox_take_dfu_request()`。

- [ ] **Step 1: 写入启动、回退和损坏状态测试**

```c
boot_policy_input_t input = valid_confirmed_a();
assert(boot_policy_decide(&input).action == BOOT_ACTION_START_A);
input.state.phase = BOOT_PHASE_PENDING_TEST;
input.state.pending_slot = FIRMWARE_SLOT_B;
input.attempts_used = 3U;
assert(boot_policy_decide(&input).action == BOOT_ACTION_ROLLBACK_TO_A);
input.state_valid = false;
assert(boot_policy_decide(&input).action == BOOT_ACTION_DFU_RECOVERY);
```

覆盖 BKP 请求、按键请求、候选无效、单槽无效、双槽无效和三次尝试耗尽。

- [ ] **Step 2: 写入一次性 mailbox 测试**

```c
assert(!boot_mailbox_take_dfu_request());
boot_mailbox_request_dfu();
assert(boot_mailbox_take_dfu_request());
assert(!boot_mailbox_take_dfu_request());
```

目标实现使用 BKP magic 和反码；两者同时匹配才接受，并在返回前清除。

- [ ] **Step 3: 注册并运行两个测试，确认失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name boot-policy`

Expected: FAIL，错误包含 `boot_policy.h` 不存在。

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name boot-mailbox`

Expected: FAIL，错误包含 `boot_mailbox.h` 不存在。

- [ ] **Step 4: 实施固定优先级决策和 mailbox**

```c
typedef enum {
    BOOT_ACTION_DFU_NORMAL,
    BOOT_ACTION_DFU_RECOVERY,
    BOOT_ACTION_START_A,
    BOOT_ACTION_START_B,
    BOOT_ACTION_ROLLBACK_TO_A,
    BOOT_ACTION_ROLLBACK_TO_B
} boot_action_t;
```

优先级为：按键恢复、BKP 正常 DFU、无有效 Boot 状态则恢复 DFU、待测试候选、已确认槽、恢复 DFU。

- [ ] **Step 5: 运行测试**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name boot-policy`

Expected: `BOOT_POLICY_TEST=PASS`。

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name boot-mailbox`

Expected: `BOOT_MAILBOX_TEST=PASS`。

- [ ] **Step 6: 提交**

```powershell
git add firmware/bootloader/boot_policy.h firmware/bootloader/boot_policy.c firmware/update/boot_mailbox.h firmware/update/boot_mailbox.c tests/boot_policy_test.c tests/boot_mailbox_test.c scripts/test_host.ps1
git commit -m "feat(boot): define startup and DFU mailbox policy"
```

---

### Task 4: 实施安全 DFU Flash 事务

**Files:**
- Create: `firmware/bootloader/dfu_flash.h`
- Create: `firmware/bootloader/dfu_flash.c`
- Create: `tests/dfu_flash_test.c`
- Modify: `scripts/test_host.ps1`

**Interfaces:**
- Consumes: 镜像验证、布局、Boot 状态和 FMC 操作。
- Produces: `dfu_flash_begin()`、`dfu_flash_write_block()`、`dfu_flash_finish()`、`dfu_flash_abort()`。

- [ ] **Step 1: 写入保护区、块顺序和失败注入测试**

```c
assert(dfu_flash_begin(&session, &header_b, FIRMWARE_SLOT_A,
                       799U, false) == DFU_FLASH_OK);
assert(dfu_flash_write_block(&session, 0U, payload,
                             sizeof(payload)) == DFU_FLASH_OK);
assert(dfu_flash_finish(&session) == DFU_FLASH_OK);
header_b.load_address = FIRMWARE_BOOT_BASE;
assert(dfu_flash_begin(&session, &header_b, FIRMWARE_SLOT_A,
                       799U, false) == DFU_FLASH_ERR_ADDRESS);
```

覆盖活动槽、Boot 状态区、配置区、跳块、重复块、擦除失败、编程失败、回读失败和最终 CRC 失败。

- [ ] **Step 2: 注册并运行测试，确认失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name dfu-flash`

Expected: FAIL，错误包含未定义的 `dfu_flash_begin`。

- [ ] **Step 3: 实施严格连续写入会话**

```c
typedef struct {
    firmware_image_header_t header;
    uint32_t next_offset;
    firmware_slot_t active_slot;
    bool active;
} dfu_flash_session_t;
```

`begin` 在擦除前验证头部；`write_block` 只接受 `offset == next_offset`；`finish` 从目标 Flash 全量重算 CRC32。

- [ ] **Step 4: 只在完整校验后提交候选状态**

```c
if (!boot_state_set_pending(header.slot,
                            header.firmware_version_code,
                            header.image_length,
                            header.image_crc32)) {
    return DFU_FLASH_ERR_STATE_COMMIT;
}
```

提交失败不得复位。

- [ ] **Step 5: 运行测试**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name dfu-flash`

Expected: `DFU_FLASH_TEST=PASS`。

- [ ] **Step 6: 提交**

```powershell
git add firmware/bootloader/dfu_flash.h firmware/bootloader/dfu_flash.c tests/dfu_flash_test.c scripts/test_host.ps1
git commit -m "feat(dfu): protect A/B flash transactions"
```

---

### Task 5: 实施独立 USB DFU 设备和 Bootloader 运行时

**Files:**
- Create: `firmware/bootloader/dfu_device.h`
- Create: `firmware/bootloader/dfu_device.c`
- Create: `firmware/bootloader/boot_board.h`
- Create: `firmware/bootloader/boot_board.c`
- Create: `firmware/bootloader/boot_led.h`
- Create: `firmware/bootloader/boot_led.c`
- Create: `firmware/bootloader/main.c`
- Create: `tests/dfu_device_test.c`
- Create: `tests/boot_runtime_test.c`
- Modify: `firmware/usb/usbd_conf.h`
- Modify: `scripts/test_host.ps1`

**Interfaces:**
- Consumes: `dfu_flash_*()`、Boot policy、Boot 状态和 mailbox。
- Produces: 独立 DFU USB 类、`dfu_device_manifest_complete()`、`boot_board_jump_to_application()` 和完整 Bootloader 主循环。

- [ ] **Step 1: 写入描述符和合法 DFU 序列测试**

断言 Bootloader PID 为 `0x1291`，应用 PID 保持 `0x1290`；接口类/子类/协议为 `0xFE/0x01/0x02`；Functional Descriptor 禁用 Upload。

```c
send_dnload_header(&udev, &valid_header);
assert(get_state(&udev) == DFU_STATE_DFU_DNLOAD_SYNC);
send_get_status(&udev);
assert(get_state(&udev) == DFU_STATE_DFU_DNLOAD_IDLE);
send_zero_length_dnload(&udev);
send_get_status(&udev);
assert(dfu_device_manifest_complete());
```

覆盖超长块、跳块、Upload、非法请求方向、ABORT、CLRSTATUS 和 Flash 错误到 `bStatus` 的映射。

- [ ] **Step 2: 写入 LED、成功复位和跳转测试**

断言等待时蓝灯 1 Hz、写入蓝快闪、校验青色、错误红灯、成功绿灯 500 ms。Manifest 成功必须先完成状态响应，再断开 USB 和复位；错误状态不得复位。

- [ ] **Step 3: 注册并运行测试，确认失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name dfu-device`

Expected: FAIL，错误包含 `dfu_device.h` 不存在。

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name boot-runtime`

Expected: FAIL，错误包含 Bootloader 运行时接口不存在。

- [ ] **Step 4: 实施项目拥有的 DFU 类**

复用 GD32 `usb_dev` 和 EP0 底层，不修改 `vendor/`。第一个 DNLOAD 块必须是 64 字节镜像头，后续块必须是连续镜像数据，零长度 DNLOAD 进入 Manifest。EP0 回调只设置 `manifest_complete`，不得直接复位。

- [ ] **Step 5: 实施最小板级层和应用跳转**

```c
bool boot_board_key_held(uint32_t stable_ms);
void boot_board_set_led(boot_led_rgb_t leds);
void boot_board_usb_connect(bool connect);
void boot_board_system_reset(void);
void boot_board_jump_to_application(uint32_t vector_base);
```

跳转前停止 SysTick，禁用并清除 NVIC 中断，设置 `SCB->VTOR` 和 MSP，再调用复位向量。按键必须稳定 50 ms。

- [ ] **Step 6: 实施 Bootloader 主循环并运行测试**

Manifest 成功后绿灯保持 500 ms，随后 USB 断开并执行 `NVIC_SystemReset()`。等待和错误路径持续服务 USB且无空闲超时。

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name dfu-device`

Expected: `DFU_DEVICE_TEST=PASS`。

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name boot-runtime`

Expected: `BOOT_RUNTIME_TEST=PASS`。

- [ ] **Step 7: 提交**

```powershell
git add firmware/bootloader firmware/usb/usbd_conf.h tests/dfu_device_test.c tests/boot_runtime_test.c scripts/test_host.ps1
git commit -m "feat(boot): add standalone USB DFU runtime"
```

---

### Task 6: 接入 MSC 入口和应用试运行确认

**Files:**
- Create: `tests/usb_config_dfu_entry_test.c`
- Create: `tests/app_boot_confirm_test.c`
- Modify: `firmware/usb/usb_config_disk.h`
- Modify: `firmware/usb/usb_config_disk.c`
- Modify: `firmware/app/main.c`
- Modify: `scripts/test_host.ps1`

**Interfaces:**
- Consumes: `boot_mailbox_request_dfu()` 和 `boot_state_confirm(firmware_slot_t running_slot)`。
- Produces: `usb_config_disk_dfu_reset_pending()` 和应用单次确认调用。

- [ ] **Step 1: 写入 MSC 精确解析和完整写入测试**

覆盖 `ENTER_DFU=1`、`0`、空值、`true`、重复字段、`ENTER_DFU =1`、近似字段、分块写入和同次普通配置修改。写入中途不得请求复位；只有整份文件有效且现有 MSC 安静窗口结束后才置位请求。

- [ ] **Step 2: 写入应用只确认一次的测试**

模拟板级、配置、USB、桥接和看门狗初始化，并运行两次主循环迭代。断言 `boot_state_confirm((firmware_slot_t)FIRMWARE_LINK_SLOT)` 只调用一次；确认失败进入运行错误状态且不重试擦写。

- [ ] **Step 3: 注册并运行测试，确认失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name usb-config-dfu-entry`

Expected: FAIL，错误包含未识别 `ENTER_DFU`。

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name app-boot-confirm`

Expected: FAIL，确认函数调用数为 0。

- [ ] **Step 4: 实施一次性配置动作**

```c
typedef struct {
    bool config_changed;
    bool enter_dfu;
} usb_config_actions_t;
```

`ENTER_DFU` 不进入 `device_config_t`，也不调用 `device_config_storage_save()`。请求接受后结果文本显示 `ENTERING_DFU`；应用完成 MSC 写事务后写 mailbox、断开 USB 并复位。

- [ ] **Step 5: 实施应用确认**

主循环第一次完整迭代后调用一次：

```c
if (!boot_confirm_attempted) {
    boot_confirm_attempted = true;
    if (!boot_state_confirm((firmware_slot_t)FIRMWARE_LINK_SLOT)) {
        runtime_error = true;
    }
}
```

非候选槽返回成功且不写 Flash。

- [ ] **Step 6: 运行新增和既有回归**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name usb-config-dfu-entry`

Expected: `USB_CONFIG_DFU_ENTRY_TEST=PASS`。

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name app-boot-confirm`

Expected: `APP_BOOT_CONFIRM_TEST=PASS`。

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name config-storage`

Expected: PASS，原配置地址和格式不变。

- [ ] **Step 7: 提交**

```powershell
git add firmware/usb/usb_config_disk.h firmware/usb/usb_config_disk.c firmware/app/main.c tests/usb_config_dfu_entry_test.c tests/app_boot_confirm_test.c scripts/test_host.ps1
git commit -m "feat(update): enter DFU through MSC configuration"
```

---

### Task 7: 生成 Bootloader、A/B 应用和工厂镜像

**Files:**
- Create: `firmware/linker/gd32f303xC_bootloader.ld`
- Create: `firmware/linker/gd32f303xC_slot_a.ld`
- Create: `firmware/linker/gd32f303xC_slot_b.ld`
- Create: `tools/build_factory_hex.py`
- Create: `tests/build_factory_hex_test.py`
- Create: `tests/firmware_layout_test.ps1`
- Modify: `scripts/build_gcc.ps1`
- Modify: `firmware/project.uvprojx`

**Interfaces:**
- Consumes: Task 1–6 的源文件和布局常量。
- Produces: `daplink_bootloader.*`、`daplink_slot_a.*`、`daplink_slot_b.*` 和 `daplink_factory.hex`。

- [ ] **Step 1: 写入布局失败测试**

测试读取 ELF section 和 Intel HEX 地址，断言三个向量表地址、区域上限和保留区边界。工厂 HEX 不得包含 `0x0803F000–0x0803FFFF`。

Run: `powershell -ExecutionPolicy Bypass -File tests/firmware_layout_test.ps1`

Expected: FAIL，因为三个新目标不存在。

- [ ] **Step 2: 新建三个链接脚本**

Bootloader 使用 `ORIGIN=0x08000000, LENGTH=16K`；A 使用 `0x08004000, 116K`；B 使用 `0x08021000, 116K`。每个脚本加入：

```ld
ASSERT((_etext - ORIGIN(FLASH)) <= LENGTH(FLASH),
       "image exceeds assigned flash region")
```

- [ ] **Step 3: 将现有构建脚本提取为目标函数**

```powershell
function Build-FirmwareTarget(
    [string]$Name,
    [string[]]$Sources,
    [string]$LinkerScript,
    [string[]]$Defines,
    [int]$FlashLimitBytes) {
    $targetBuildDir = Join-Path $buildDir $Name
    New-Item -ItemType Directory -Force -Path $targetBuildDir | Out-Null
    $objects = Invoke-CompileSources -Sources $Sources `
        -Defines $Defines -OutputDirectory $targetBuildDir
    Invoke-LinkTarget -Name $Name -Objects $objects `
        -LinkerScript $LinkerScript -OutputDirectory $targetBuildDir `
        -FlashLimitBytes $FlashLimitBytes
}
```

同一步先从当前编译循环提取 `Invoke-CompileSources`，再从当前链接、size、objcopy 和 map 逻辑提取 `Invoke-LinkTarget`。这两个函数必须保留当前错误码检查、栈使用检查和 Release manifest 数据。不得复制三套会漂移的编译逻辑。A/B 分别定义 `FIRMWARE_LINK_SLOT=0` 和 `1`。Bootloader 不链接无线、CDC、MSC 或 CMSIS-DAP。

- [ ] **Step 4: 生成不覆盖配置页的工厂 HEX**

`tools/build_factory_hex.py` 解析三个输入 HEX 的地址记录，拒绝重叠地址，并输出带正确扩展线性地址和校验和的 Intel HEX。`tests/build_factory_hex_test.py` 必须覆盖重叠、错误校验和、越过配置区和成功合并。只合并 Bootloader、Slot A 和初始化 Boot 状态记录；不得生成填充整个 256 KiB 的 BIN。

- [ ] **Step 5: 运行 Debug、Release 和布局测试**

Run: `powershell -ExecutionPolicy Bypass -File scripts/build_gcc.ps1 -Configuration Debug`

Expected: 三个目标构建成功且区域检查通过。

Run: `powershell -ExecutionPolicy Bypass -File scripts/build_gcc.ps1 -Configuration Release`

Expected: 三个 Release 目标构建成功。

Run: `powershell -ExecutionPolicy Bypass -File tests/firmware_layout_test.ps1`

Expected: `FIRMWARE_LAYOUT_TEST=PASS`。

Run: `python -m unittest tests.build_factory_hex_test -v`

Expected: 全部用例 PASS。

- [ ] **Step 6: 提交**

```powershell
git add firmware/linker/gd32f303xC_bootloader.ld firmware/linker/gd32f303xC_slot_a.ld firmware/linker/gd32f303xC_slot_b.ld tools/build_factory_hex.py tests/build_factory_hex_test.py scripts/build_gcc.ps1 tests/firmware_layout_test.ps1 firmware/project.uvprojx
git commit -m "build: add bootloader and A/B firmware targets"
```

---

### Task 8: 实施 `.dwup` 包和主机升级工具

**Files:**
- Create: `tools/dwup_format.py`
- Create: `tools/build_dwup.py`
- Create: `tools/daplink_updater.py`
- Create: `tests/dwup_format_test.py`
- Create: `tests/daplink_updater_test.py`
- Modify: `scripts/build_gcc.ps1`

**Interfaces:**
- Consumes: A/B BIN、版本代码、DFU 接口字符串和外部 `dfu-util`。
- Produces: `read_dwup()`、`write_dwup()`、`daplink_wireless.dwup` 以及 `inspect`、`enter-dfu`、`update` 子命令。

- [ ] **Step 1: 写入包往返和损坏输入测试**

```python
package = build_package(version_code=800,
                        slot_a=(0x08004000, b"A" * 64),
                        slot_b=(0x08021000, b"B" * 64))
decoded = decode_package(package)
assert decoded.version_code == 800
assert decoded.images["A"].load_address == 0x08004000
assert decoded.images["B"].load_address == 0x08021000
```

覆盖截断、未知格式、重复槽、错误地址、版本不一致、头 CRC、镜像 CRC 和尾随数据。

- [ ] **Step 2: 写入 updater 的纯函数和进程桩测试**

```python
updated = set_enter_dfu("MODE=WIRED\r\nPROFILE=AUTO\r\n", True)
assert updated.endswith("ENTER_DFU=1\r\n")
info = parse_dfu_listing(sample_dfu_util_output)
assert info.inactive_slot == "B"
assert info.load_address == 0x08021000
assert info.version_code == 799
```

验证子进程参数为列表且不使用 `shell=True`；覆盖找不到磁盘、多个设备、错误产品、版本拒绝、恢复模式和 `dfu-util` 非零退出。

- [ ] **Step 3: 运行测试，确认失败**

Run: `python -m unittest tests.dwup_format_test tests.daplink_updater_test -v`

Expected: FAIL，错误包含 `No module named 'tools.dwup_format'`。

- [ ] **Step 4: 实施固定小端包格式**

使用 `struct.Struct` 和 `zlib.crc32`。所有偏移和长度在切片前检查。禁止使用 `pickle` 或依赖平台 ABI 的结构体。

```python
PACKAGE_HEADER = struct.Struct("<4sHHIIII")
IMAGE_ENTRY = struct.Struct("<B3xIIII32s")
```

A/B 条目必须各出现一次，且版本和源码标识一致。

- [ ] **Step 5: 实施 MSC 触发、重新枚举和下载**

Bootloader DFU 接口字符串固定为：

```text
DAPLink-Wireless;inactive=B;addr=0x08021000;version=799;mode=normal
```

工具修改 `CONFIG.TXT` 后等待应用卷消失，再轮询 `dfu-util --list`。选择相同槽和地址的镜像，生成“64 字节设备镜像头 + 镜像体”的临时文件，并执行：

```python
["dfu-util", "--device", "28e9:1291",
 "--alt", "0", "--download", payload_path]
```

临时文件必须由 `TemporaryDirectory()` 管理。主机等待超时不得改变设备端无空闲超时规则。

- [ ] **Step 6: 接入 Release 构建并运行测试**

Run: `python -m unittest tests.dwup_format_test tests.daplink_updater_test -v`

Expected: 全部 PASS，不访问真实 USB。

Run: `powershell -ExecutionPolicy Bypass -File scripts/build_gcc.ps1 -Configuration Release`

Expected: 输出包含以 `Built:` 开头且以 `daplink_wireless.dwup` 结尾的产物路径。

- [ ] **Step 7: 提交**

```powershell
git add tools/dwup_format.py tools/build_dwup.py tools/daplink_updater.py tests/dwup_format_test.py tests/daplink_updater_test.py scripts/build_gcc.ps1
git commit -m "feat(tools): package and install A/B DFU updates"
```

---

### Task 9: 接入发布门禁、文档和完整软件回归

**Files:**
- Modify: `scripts/verify_release.ps1`
- Modify: `scripts/package_release.ps1`
- Modify: `docs/development_release_manual.md`
- Modify: `docs/project_manual.md`
- Modify: `docs/hardware_manual.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: 所有构建产物、测试和工具。
- Produces: 可发布 DFU 包、工厂镜像、升级说明和硬件验收清单。

- [ ] **Step 1: 先增加失败的发布产物断言**

```powershell
$requiredDfuArtifacts = @(
    "daplink_bootloader.elf",
    "daplink_slot_a.bin",
    "daplink_slot_b.bin",
    "daplink_wireless.dwup",
    "daplink_factory.hex"
)
```

Run: `powershell -ExecutionPolicy Bypass -File scripts/verify_release.ps1 -SkipKeil`

Expected: 在发布脚本尚未复制新产物时 FAIL，并明确列出缺失文件。

- [ ] **Step 2: 更新发布打包和文档**

发布 ZIP 必须包含 `.dwup`、factory HEX、升级工具和使用说明。文档必须包含首次 SWD 部署、`ENTER_DFU=1`、正常/恢复版本规则、自动复位、三次回退、LED 状态、Windows/Linux `dfu-util` 前置条件、配置保留和实板边界。

- [ ] **Step 3: 运行全部主机和 Python 测试**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1 -Name all`

Expected: 全部 C 主机测试 PASS。

Run: `python -m unittest discover -s tests -p "*_test.py" -v`

Expected: 全部 Python 测试 PASS。

- [ ] **Step 4: 运行构建和发布门禁**

Run: `powershell -ExecutionPolicy Bypass -File scripts/build_gcc.ps1 -Configuration Debug`

Expected: Bootloader、A、B Debug 构建成功。

Run: `powershell -ExecutionPolicy Bypass -File scripts/verify_release.ps1 -SkipKeil`

Expected: Release 构建、包检查、软件回归和发布打包通过。

Run: `git diff --check`

Expected: 无输出，退出码为 0。

- [ ] **Step 5: 提交**

```powershell
git add scripts/verify_release.ps1 scripts/package_release.ps1 docs/development_release_manual.md docs/project_manual.md docs/hardware_manual.md README.md
git commit -m "docs: document USB DFU release workflow"
```

---

### Task 10: 首次实板部署和 DFU 验收

**Files:**
- Modify: `docs/development_release_manual.md`（只记录实际观察结果）
- Modify: `CHANGELOG.md`（软件和硬件门禁满足后）

**Interfaces:**
- Consumes: `daplink_factory.hex`、`.dwup`、升级工具、调试器和真实硬件。
- Produces: 可重复的实板验收记录。

- [ ] **Step 1: 在烧录前保存配置页**

读取 `0x0803F000–0x0803FFFF` 并保存为 4 KiB 文件。核对长度和 SHA-256。不得执行整片擦除。

- [ ] **Step 2: 首次烧录工厂镜像**

使用 SWD halted/under-reset 模式烧录 `daplink_factory.hex`。再次读取配置页并确认 SHA-256 不变。

- [ ] **Step 3: 验证 MSC 进入 DFU 和 LED**

在 Windows 修改 `CONFIG.TXT` 为 `ENTER_DFU=1`。观察应用 PID 断开、DFU PID 枚举和蓝灯慢闪。核对实板颜色；如果颜色对调，先修复板级映射和测试。

- [ ] **Step 4: 验证成功升级和自动复位**

从 A 升级到 B。观察蓝灯快闪、青色校验、绿色约 500 ms、自动复位和应用重新枚举。读取 Boot 状态，确认 B 从 `PENDING_TEST` 变为已确认槽。

- [ ] **Step 5: 验证中断和回退**

在擦除和写入阶段分别拔除 USB，重新上电后确认旧槽仍运行。烧录一个不调用确认的候选镜像，确认三次看门狗复位后回退旧槽，并观察红蓝交替。

- [ ] **Step 6: 验证恢复模式和跨平台**

使两个应用槽无效，按住按键上电，确认仍进入恢复 DFU。用恢复模式刷入较低版本，并验证 CRC/型号限制仍有效。在 Linux 重复一次完整升级。

- [ ] **Step 7: 记录证据并提交**

只记录实际命令、工具版本、USB VID/PID、LED 观察、Flash 地址、通过项和未通过项。不得把未运行的物理测试标为完成。

```powershell
git add docs/development_release_manual.md CHANGELOG.md
git commit -m "test(hardware): record USB DFU acceptance"
```
