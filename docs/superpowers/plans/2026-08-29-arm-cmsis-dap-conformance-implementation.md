# Arm CMSIS-DAP Conformance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 以 Arm CMSIS-DAP 2.1.2 为协议判定基准，补齐 Queue/ExecuteCommands 并恢复完整无线通信修复。

**Architecture:** 官方 `DAP.c` 仅在主机测试中作为同步协议判定器。生产固件保留异步状态机，并增加聚合命令上下文；USB 传输层按 Arm 模板处理 Queue 和 Abort。

**Tech Stack:** C11、Arm CMSIS-DAP 2.1.2、GD32 USB device、SX1281 FLRC、PowerShell、GCC 主机测试。

**Spec:** `docs/superpowers/specs/2026-08-29-arm-cmsis-dap-conformance-design.md`

## Global Constraints

- 不增加私有 Keil 追踪命令。
- 保留标准 GUID、4 MHz SWD 上限和无线协议 v3。
- 保留当前脏工作树，不执行 reset、clean、commit 或 push。
- 软件门禁、双板刷写和 Keil 实机验收分别记录。

---

### Task 1: 恢复通信链路修复

**Files:**
- Modify: `firmware/app/serial_bridge_scheduler.h`
- Modify: `firmware/app/firmware_version.h`
- Modify: `tests/serial_bridge_hot_path_test.c`

**Interfaces:**
- Produces: 1.0.19 的紧凑 ACK、packet status、隐式确认和快速请求重试行为。

- [ ] 修改热路径测试，使其要求固定 profile 紧凑 ACK、不读取 packet status、隐式确认和 12–19 ms 请求 ACK 窗口。
- [ ] 运行 `pwsh -NoProfile -File .\scripts\test_host.ps1 -Name serial-bridge-hot-path`，确认测试失败。
- [ ] 恢复 `serial_bridge_scheduler.h` 的优化行为。
- [ ] 重跑 focused test，确认通过。

### Task 2: 建立官方协议判定基准

**Files:**
- Create: `tests/arm_cmsis_dap_oracle_config/DAP_config.h`
- Create: `tests/arm_cmsis_dap_oracle_test.c`
- Modify: `scripts/test_host.ps1`

**Interfaces:**
- Consumes: `vendor/Arm-CMSIS-DAP/Firmware/Source/DAP.c`。
- Produces: 可重复的官方命令响应和 SWD 调用顺序测试。

- [ ] 用确定性 SWD 后端编写官方 `DAP.c` 判定测试。
- [ ] 将测试加入 `test_host.ps1`，运行并确认构建或断言失败。
- [ ] 补齐官方核心所需的最小配置和板级桩函数。
- [ ] 运行判定测试，确认 Transfer/TransferBlock 基准通过。

### Task 3: 实现 ExecuteCommands 异步聚合

**Files:**
- Modify: `tests/cmsis_dap_protocol_test.c`
- Modify: `firmware/app/cmsis_dap.c`
- Modify: `firmware/app/cmsis_dap.h`

**Interfaces:**
- Consumes: `cmsis_dap_submit()`、`cmsis_dap_process()`、`cmsis_dap_response_take()`。
- Produces: `0x7F` 外层响应和按顺序连接的子命令响应。

- [ ] 增加两个同步子命令和同步后异步子命令测试。
- [ ] 运行 `cmsis-dap` focused test，确认当前返回 `DAP_Invalid`。
- [ ] 增加聚合请求偏移、响应偏移、剩余命令数和子命令状态。
- [ ] 在每条子命令完成后推进下一条，完成后返回官方外层响应。
- [ ] 增加截断和溢出保护测试并确认通过。

### Task 4: 实现 USB QueueCommands 语义

**Files:**
- Modify: `tests/cmsis_dap_usb_transport_test.c`
- Modify: `firmware/usb/cmsis_dap_usb.c`

**Interfaces:**
- Consumes: 四槽 USB 请求环和 `cmsis_dap_submit()`。
- Produces: `0x7E` 延后执行及后续请求触发行为。

- [ ] 增加 Queue 不立即提交、后续请求触发和 Abort 不入队测试。
- [ ] 运行 `cmsis-dap-usb` focused test，确认 Queue 测试失败。
- [ ] 按 Arm USB 模板实现 Queue 等待和 `0x7E -> 0x7F` 转换。
- [ ] 重跑 USB focused test，确认通过。

### Task 5: 发布和硬件验收

**Files:**
- Modify: `firmware/app/firmware_version.h`
- Modify: `CHANGELOG.md`
- Modify: `docs/wireless_manual.md`

**Interfaces:**
- Produces: 新版本 `.dwup` 和 Keil 实机结果。

- [ ] 迭代版本并记录官方一致性修复。
- [ ] 运行 `pwsh -NoProfile -File .\scripts\verify_release.ps1`。
- [ ] 先升级无线从机，再升级无线主机。
- [ ] 核对双板版本、模式、FLRC profile 和 pyOCD 枚举。
- [ ] 执行 Keil Flash Download，并单独记录结果。

