# Wired CMSIS-DAP Speed Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve wired CMSIS-DAP programming throughput by allowing bounded USB request/response pipelining and by removing avoidable SWD bit-bang overhead, while preserving CMSIS-DAP ordering, abort behavior, the wireless path, and the existing full-speed USB packet size.

**Architecture:** Keep the CMSIS-DAP command core single-threaded and execute one SWD operation at a time, but add four-entry request and response rings around it. Report the same four-entry capacity through `DAP_Info`; do not advertise a larger capacity than the firmware can store. In the SWD driver, preserve the existing signal phase and make the requested clock ceiling explicit at 4 MHz while replacing repeated generic GPIO reconfiguration with direct PB12 mode changes.

**Tech Stack:** C11 firmware, GD32F30x GPIO/USB library, PowerShell host-test/build scripts, GCC host regression tests.

**Spec:** Approved in-chat design from 2026-08-25 wired CMSIS-DAP performance diagnosis.

## Global Constraints

- Keep `DAP_USB_PACKET_SIZE` at 64 bytes because this device exposes full-speed USB bulk endpoints.
- Preserve response order and `DAP_TransferAbort` handling.
- Current wired optimization tasks must not change the wireless radio protocol or
  wireless bridge behavior.
- Any subsequent wireless throughput redesign must target
  `RADIO_PROTOCOL_VERSION 2U` as the only wire protocol. The implementation
  must not retain v1 parsing, v1 transmission, a dual-stack compatibility shim,
  fallback negotiation, or a migration handshake.
- Do not erase or alter bootloader, parameter, or unrelated target flash regions as part of verification.
- Preserve all unrelated existing dirty work in the checkout; do not commit or push.
- A higher SWD clock is a capability ceiling, not hardware acceptance; report physical target validation separately.

---

## 全项目性能审查结论（2026-08-26 更新）

本节记录对第一方 `firmware/`、`scripts/` 和 `tests/` 的只读性能审查结果。
`vendor/` 和 `Third-Party/` 未纳入可修改范围。

### 已确认的证据

- 优化前基线为 15 组主机回归测试全部通过，`git diff --check` 无错误。
- 优化前 Release manifest 记录 `flash_bytes=46448`、`ram_bytes=29508`、最大函数栈
  `432` 字节。BIN 文件因对齐为 `46464` 字节；Flash/RAM 统计以 ELF 的 text/data/bss
  为准。
- Release 使用全局 `-Os`。当前 `target_swd_transfer` 保留独立的
  `clock_delay`、`clock_cycle`、`write_bit` 和 `read_bit` 函数。临时使用相同
  目标参数以 `-O3` 编译时，这些辅助函数全部内联，函数大小由 484 字节增加到
  2186 字节。临时编译只比较代码形态，没有测量硬件吞吐。
- 当前仍没有可用的实机验收证据。因此，已有软件门禁不代表实际 SWCLK、烧录速度、
  USB/CDC 吞吐、无线 goodput、UART 数据完整性或 Abort 恢复已经通过。
- 已定位并修复一个会直接延迟 DAP 识别的启动时序问题：旧版 `main()` 先调用
  `serial_bridge_init()`。在无线模式下，`serial_bridge_init()` 会同步执行 SX1281
  初始化，USB D+ 上拉直到无线初始化完成后才建立。当前 `main()` 先调用
  `usb_config_disk_init()`，再调用 `serial_bridge_init()`；主机因此可以先开始 USB
  枚举。源码检查结果仍需真实板卡记录上电到 `VID_28E9&PID_1290` 出现的时间。

### 当前软件门禁证据（2026-08-26）

- `scripts/test_host.ps1 -Name all`：16 组主机测试全部通过。
- `scripts/build_gcc.ps1 -Configuration Debug`：通过。
- `scripts/build_gcc.ps1 -Configuration Release`：通过，text/data/bss 为
  `45252/1784/28324` 字节。
- Release manifest：`flash_bytes=47036`、`ram_bytes=30108`、最大函数栈
  `432` 字节；BIN 文件为 `47052` 字节。
- `scripts/verify_release.ps1 -SkipKeil` 和 `git diff --check`：通过。
- 主机测试、GCC 构建和 Release manifest 仅证明软件门禁通过，不证明真实 DAP 枚举、
  SWCLK、烧录速度、无线 goodput 或 UART/CDC 数据完整性。

### 优先级结论

| 优先级 | 结论 | 依据 | 后续动作 |
| --- | --- | --- | --- |
| P0 | SWD 位操作是有线烧录的首要优化点 | 120 MHz CPU 在 4 MHz SWD 下每个半周期只有 15 个 CPU 周期；当前热路径包含多层函数调用和多次 `DWT->CYCCNT` 读取 | 仅对 `target_swd.c` 使用 `-O3`，内联 DWT/GPIO 热路径；用逻辑分析仪测量 100 kHz、1 MHz、2 MHz、4 MHz 的实际 SWCLK |
| P0 | CDC 应用长度超过端点包长 | CDC 端点为 64 字节，但服务层和无线 DATA 允许 110 字节；单包 TX 缓冲会拒绝或消费后丢失大块数据 | 已增加有界 CDC TX 队列并拆包；源端只有在完整入队后消费 |
| P0 | DATA 窗口满时源数据仍被消费 | `wireless_source_process()` 忽略 `radio_window_tx_push()` 失败结果 | 已增加窗口容量检查和源端背压 |
| P1 | SWD 热路径仍调用外部 DWT 读取函数 | 120 MHz、4 MHz SWD 时每半周期约 15 个 CPU 周期 | 已在 `target_swd.c` 直接读取 `DWT->CYCCNT`；仍需测量 1/2/4 MHz 实际波形 |
| P1 | CMSIS-DAP chunk 仍限制为 10 | v2 压缩块支持 21 个 transfer，但上层会把合法大请求拆成 10+余数 | 已将内部 chunk 上限提高到 16，并保留 CMSIS-DAP 包长度校验 |
| P1 | 无线半双工 ACK 时序未定义 | 立即 ACK 与 TX_DONE 后的 DATA 发送可能碰撞 | 双板测量 window=1/4 和立即/延迟/聚合 ACK 后确定调度 |
| P1 | 异步 SWD WAIT 首次返回即结束 | `target_swd_transfer_poll()` 未保留 WAIT 重试状态 | 已增加预算内 WAIT 重试并覆盖主机替身回归；真实目标 WAIT 仍待验收 |
| P2 | UART DMA 环满后继续覆盖未读数据 | 3 Mbps 下 511 字节有效容量约 1.70 ms 即可填满 | 增加流控/staging 或明确溢出策略并做持续 PRBS 压测 |
| P2 | SX1281 packet params 单值缓存 | RX 最大长度、DATA 长度和 ACK 长度交替会反复发送配置命令 | 已增加四项有限多值缓存；之后再评估 SPI DMA |

USB 四包请求/响应环已经解决主要的主机往返等待问题。扩大到八包只能作为
A/B 测试，不能替代 SWD 热路径优化。真实烧录速度必须使用同一 Release BIN，
分别测量 1 MHz、2 MHz 和 4 MHz 的擦除、编程、总耗时和 CRC/readback；软件
测试结果不得替代硬件验收。

### 协议 v2 的单一版本决策

当前实现已切换到 `RADIO_PROTOCOL_VERSION 2U`。v1 仅作为历史问题记录，不再作为
运行时兼容路径或文档操作步骤。

开始无线吞吐重构后，协议 v2 的要求如下：

- v2 是唯一支持的无线协议版本，版本字段使用 `2U`。
- 接收端只接受 v2 帧；v1 帧必须拒绝。
- 发送端只发送 v2 帧；不得保留 v1 发送路径。
- 不实现双栈、兼容层、版本协商、回退路径或迁移握手。
- v2 的帧格式可以直接替换 v1 的帧格式。协议测试、Release manifest、无线手册
  和硬件验收必须同时切换到 v2。
- v2 的设计目标是将应用负载上限从当前 64 字节提高到
  `SX128X_MAX_PAYLOAD_SIZE - RADIO_PROTOCOL_HEADER_SIZE = 110` 字节，并为
  SWD block、DATA 窗口和累计 ACK 预留明确的帧类型与状态机。

无线运行文档已按 v2 更新。真实双板验收仍待硬件和探针可用后执行。

---

### Task 1: Establish the failing transport regression

**Files:**
- Modify: `tests/cmsis_dap_usb_transport_test.c`
- Modify: `tests/cmsis_dap_protocol_test.c`
- Modify: `firmware/app/cmsis_dap.h`

**Interfaces:**
- The protocol test will consume `CMSIS_DAP_PACKET_COUNT` and verify `DAP_Info(0xFE)` reports it.
- The USB transport test will consume `DAP_USB_PACKET_COUNT` and verify multiple ordinary packets can be accepted while one core operation is busy.

- [x] **Step 1: Add the packet-count contract assertion.**

  Extend the existing CMSIS-DAP protocol test with a request `{0x00, 0xFE}` and assert response bytes `{0x00, 0x01, CMSIS_DAP_PACKET_COUNT}`.

- [x] **Step 2: Add a pipelined USB test before production changes.**

  Drive two ordinary packets through `data_out` before making the first response ready. Assert that the first packet is submitted immediately, the second remains queued, and the abort command still increments the abort counter without creating a response.

- [x] **Step 3: Run the focused tests and capture RED.**

  Run:

  ```powershell
  .\scripts\test_host.ps1 -Name cmsis-dap
  .\scripts\test_host.ps1 -Name cmsis-dap-usb
  ```

  Expected: the protocol assertion fails because the current implementation reports `1`; the USB test fails because the current transport ignores ordinary packets received while `abort_receive_active` is set.

### Task 2: Add the four-packet CMSIS-DAP request/response rings

**Files:**
- Modify: `firmware/app/cmsis_dap.h`
- Modify: `firmware/app/cmsis_dap.c`
- Modify: `firmware/usb/cmsis_dap_usb.h`
- Modify: `firmware/usb/cmsis_dap_usb.c`
- Modify: `tests/cmsis_dap_usb_transport_test.c`

**Interfaces:**
- `CMSIS_DAP_PACKET_COUNT` is the single source of truth and equals `4U`.
- `DAP_USB_PACKET_COUNT` aliases `CMSIS_DAP_PACKET_COUNT` for transport storage.
- `cmsis_dap_usb_idle()` remains true only when the request ring, response ring, USB IN transfer, and CMSIS-DAP core are all idle.

- [x] **Step 1: Define the capacity and transport packet type.**

  Add `#define CMSIS_DAP_PACKET_COUNT 4U` beside `CMSIS_DAP_PACKET_SIZE`. Add a `DAP_USB_PACKET_COUNT` alias and an internal ring storage size of `DAP_USB_PACKET_COUNT + 2U`; the extra slot preserves room for `DAP_TransferAbort` after four ordinary requests are queued.

- [x] **Step 2: Replace the single request/abort/response buffers.**

  Store request and response packets as ring entries containing a 64-byte data array and an 8-bit length. Track producer and consumer indices as separate `volatile uint8_t` values. Keep `tx_busy`; remove `abort_receive_active` because abort is recognized directly from the received packet's first byte.

- [x] **Step 3: Re-arm OUT only when a request slot exists.**

  In `data_out`, treat zero-length packets as dropped, call `cmsis_dap_abort()` for `DAP_TRANSFER_ABORT`, otherwise record the received length and advance the request write index. Re-arm the endpoint into the next free request slot. If the ring is full, leave the endpoint unarmed until the main loop consumes a slot; do not overwrite or drop an accepted ordinary packet.

- [x] **Step 4: Consume one request at a time and enqueue responses.**

  In `cmsis_dap_usb_process`, submit the oldest queued request only when `cmsis_dap_busy()` is false, run `cmsis_dap_process`, and copy a ready response into the response ring only when a response slot is available. Advance the request read index only after `cmsis_dap_submit` succeeds.

- [x] **Step 5: Serialize only the USB IN endpoint.**

  Send the oldest response ring entry when `tx_busy` is false. In `data_in`, advance the response read index, clear `tx_busy`, re-arm OUT if space is available, and immediately start the next response if present. Preserve FIFO order.

- [x] **Step 6: Update idle and deinit state.**

  Reset all indices and flags in `dap_usb_init`/`dap_usb_deinit`. Make `cmsis_dap_usb_idle` include both rings and `cmsis_dap_busy`.

- [x] **Step 7: Run the focused tests and capture GREEN.**

  Run:

  ```powershell
  .\scripts\test_host.ps1 -Name cmsis-dap
  .\scripts\test_host.ps1 -Name cmsis-dap-usb
  ```

  Expected: both focused tests pass, including packet count, queued ordinary requests, FIFO response order, and abort recovery.

### Task 3: Reduce SWD bit-bang overhead and expose a safe clock ceiling

**Files:**
- Modify: `firmware/drivers/swd/target_swd.c`
- Modify: `firmware/drivers/swd/target_swd.h`
- Create: `tests/target_swd_config_test.c`
- Modify: `scripts/test_host.ps1`

**Interfaces:**
- Add a pure `static inline target_swd_normalize_clock(uint32_t clock_hz)` helper in `target_swd.h` that maps zero to 100 kHz, clamps below 10 kHz, and clamps above 4 MHz. `target_swd_init` uses this helper.
- Keep the current SWD clock phase and idle/turnaround semantics unchanged.

- [x] **Step 1: Write the clock-normalization regression.**

  Add host assertions for `0U -> 100000U`, `1000U -> 10000U`, `2000000U -> 2000000U`, and `10000000U -> 4000000U`. Register the executable under the `target-swd-config` key in `scripts/test_host.ps1` with `firmware/drivers/swd` as its include directory.

- [x] **Step 2: Run the new test before implementation.**

  Run:

  ```powershell
  .\scripts\test_host.ps1 -Name target-swd-config
  ```

  Expected: compilation fails because the helper does not yet exist.

- [x] **Step 3: Implement the pure helper and use it in `target_swd_init`.**

  Replace the 1 MHz ceiling with 4 MHz through the helper. Keep the 100 kHz default and 10 kHz minimum.

- [x] **Step 4: Replace repeated `gpio_init` calls for SWDIO direction changes.**

  Add PB12-specific direct `GPIO_CTL1` mode updates that preserve the other PB12 control bits, use push-pull 50 MHz for output, and use pull-up input for input. Continue using `gpio_init` during initial pin setup and disconnect; the transfer hot path must not call the generic loop over all 16 pins.

- [x] **Step 5: Run the new test and the SWD tunnel regression.**

  Run:

  ```powershell
  .\scripts\test_host.ps1 -Name target-swd-config
  .\scripts\test_host.ps1 -Name swd-tunnel
  ```

  Expected: both pass; no wireless tunnel behavior changes.

### Task 4: Build and software regression gate

**Files:**
- Modify: `docs/superpowers/plans/2026-08-25-wired-dap-speed-optimization.md`

- [x] **Step 1: Run all host tests.**

  ```powershell
  .\scripts\test_host.ps1 -Name all
  ```

- [x] **Step 2: Build Debug and Release firmware.**

  ```powershell
  .\scripts\build_gcc.ps1 -Configuration Debug
  .\scripts\build_gcc.ps1 -Configuration Release
  ```

- [x] **Step 3: Check the diff and generated manifest.**

  ```powershell
  git diff --check
  git diff --stat
  ```

  Confirm the release manifest still identifies CMSIS-DAP v2 and that no unrelated dirty file was reverted.

### Review follow-up

- [x] Mask GD32 GPIO mode constants to the low four-bit PB12 field and add a PB13-preservation regression.
- [x] Reserve an internal USB request slot so `DAP_TransferAbort` remains deliverable after four ordinary requests are queued.
- [x] Synchronize the top-level README and documentation index with the v2-only wire protocol and `RADIO_PROTOCOL_VERSION 2U`.
- [x] Re-run focused tests, the full host suite, both GCC configurations, release verification, and `git diff --check`.

### Task 5: Hardware performance acceptance gate

**Files:**
- No source changes; record results in the final handoff only.

**当前状态（2026-08-25）：暂缓。** 用户当前无法提供硬件验收条件，因此以下步骤保持未勾选。
软件测试、构建和 Release 验证不等价于目标板上的 SWCLK、烧录吞吐、数据完整性或 Abort 恢复验收。

- [ ] **Step 1: Flash the verified Release artifact with the user's existing pyOCD load command.**

  Keep the binary path and target identifier from the user's load command, retain base address `0x08004000`, and add only `--frequency 1m`, `--frequency 2m`, or `--frequency 4m`. Keep bootloader and parameter regions intact. Alternate two deterministic slot images so each run changes the same 39 sectors.

- [ ] **Step 2: Measure three requested clocks.**

  Run the same pyOCD load at `1m`, `2m`, and `4m`, record erase time, programming time, total time, and verification result. A request above 4 MHz must not be used as evidence because the firmware clamps it.

- [ ] **Step 3: Verify data integrity and abort recovery.**

  Compare target CRC/readback after each load and issue an abort during a controlled long transfer once. Report hardware results separately from software test results.

## 跟进实施顺序（2026-08-26）

具体实现、失败回归和每阶段门禁见
[`2026-08-26-throughput-followup-implementation.md`](2026-08-26-throughput-followup-implementation.md)。
执行顺序固定为：CDC 队列与源端背压、DATA 窗口容量保护、CMSIS-DAP chunk、异步
WAIT 重试、SWD/SX1281 热路径，最后才进行双板和目标板性能验收。

### USB 枚举延迟专项结论

当前固件的 USB 初始化顺序早于无线桥接初始化。这个启动顺序只缩短“设备开始响应 USB
枚举”的等待，不改变 Windows 驱动绑定、USB-C CC、电缆、D+/D− 走线、R19 上拉、USB
48 MHz 时钟或实际板卡电源质量。若软件门禁通过后仍长时间看不到 DAP，应记录 MCU
上电、D+ 上拉和 USB 总线枚举时序，并检查前述 USB 硬件条件。
