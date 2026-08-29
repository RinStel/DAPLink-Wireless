# 无线吞吐诊断实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不改变无线协议行为的条件下，采集 USB、无线 ACK、SWD 响应和 USB IN 的端到端计数与周期数据。

**Architecture:** `dap_diagnostics` 作为独立统计模块；所有热路径调用由 `CMSIS_DAP_DIAGNOSTICS_ENABLE` 条件编译。`DAP_VENDOR_TRACE (0x81)` 提供清零和固定分页读取，主机工具输出 JSON。Remote 内部统计不经无线转发。

**Tech Stack:** C11、CMake、Cortex-M4 DWT、CMSIS-DAP v2 Bulk、Python/PyUSB。

**Spec:** 用户于 2026-08-29 批准的主机侧最小采集设计。

## Global Constraints

- 不修改无线帧格式、ACK 语义、窗口、profile 或 SWD 时钟。
- 关闭 `CMSIS_DAP_DIAGNOSTICS_ENABLE` 时，不得保留热路径函数调用、时间戳读取或统计存储。
- 软件门禁不替代 64 KiB 实板吞吐验收。
- 不提交、不推送、不清理现有工作树。

---

### Task 1: 条件编译与统计模型

**Files:** `CMakeLists.txt`、`firmware/app/dap_diagnostics.h`、`firmware/app/dap_diagnostics.c`

- [ ] 增加显式 CMake 选项和关闭时为空操作的 hook 宏。
- [ ] 记录命令分布、transfer 直方图、RF 帧/字节/重传、ACK、SWD 响应和 USB IN 周期。
- [ ] 增加饱和累计和 32 位周期回绕安全差值。

### Task 2: 真实状态机埋点与 0x81 分页

**Files:** `firmware/usb/cmsis_dap_usb.c`、`firmware/app/serial_bridge.c`、`firmware/app/cmsis_dap.c`、`tests/cmsis_dap_protocol_test.c`

- [ ] 先写 `0x81` 清零和分页读取失败测试。
- [ ] 在 USB OUT/IN、可靠 TX/TX_DONE、匹配 ACK 和 SWD 响应接收点接入 hook。
- [ ] 返回版本化的固定 64 字节统计页，诊断关闭时返回 Invalid。

### Task 3: 采集工具、版本和验证

**Files:** `tools/dap_diagnostics.py`、`firmware/app/firmware_version.h`、`CHANGELOG.md`

- [ ] 实现 `reset`、`dump` 和 JSON 派生指标。
- [ ] 版本递增到 `1.0.33`，记录诊断构建方法。
- [ ] 运行 focused tests、完整发布门禁、诊断 Release 构建和 `git diff --check`。
