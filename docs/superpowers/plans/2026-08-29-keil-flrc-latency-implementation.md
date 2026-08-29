# Keil 枚举与 FLRC 延迟修复实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复 Keil 对 CMSIS-DAP v2 的枚举，并消除 FLRC 下每约 32 次事务出现的 600 ms 级跳频长尾；内部无线协议迭代为 v3。

**Architecture:** USB 描述符改用 Arm 标准设备接口 GUID。无线 v3 保留通用帧头和 CMSIS-DAP 边界；固定 profile 使用紧凑 ACK，SWD 请求到达与执行完成继续分阶段确认。携带跳频目标的 ACK 发送方在 ACK `TX_DONE` 后切换，接收方在收到 ACK 后切换。周期跳频只由 DATA 推进，健康 SWD 只在实际失败时进入恢复换频。

**Tech Stack:** C11、GD32 USB device library、SX1281 FLRC、CMSIS-DAP v2、PowerShell、GCC 主机测试、pyOCD、Keil UV4。

**Spec:** `docs/superpowers/specs/2026-08-29-keil-flrc-latency-design.md`

## Global Constraints

- `RADIO_PROTOCOL_VERSION` 必须为 `3U`；不保留 v2/v1 无线兼容路径。
- 保持 17 字节通用帧头和 110 字节应用负载上限；固定 profile ACK 可使用 8 字节布局。
- 保持 USB VID/PID、接口号、端点和包长不变。
- 保留现有脏工作树，不得重置、清理、提交或推送。
- 软件门禁、无线时延和 Keil 下载分别记录。

---

### Task 1: 标准 CMSIS-DAP v2 GUID

**Files:**
- Modify: `tests/usb_composite_descriptor_test.c`
- Modify: `firmware/usb/usb_composite.c`

**Interfaces:**
- Consumes: Microsoft OS 1.0 extended property descriptor。
- Produces: `DeviceInterfaceGUIDs={CDB3B5AD-293B-4663-AA36-1AAE46463776}`。

- [x] **Step 1: 写入失败测试。**

  将属性数据复制为 ASCII 字符后，断言它等于
  `"{CDB3B5AD-293B-4663-AA36-1AAE46463776}"`，并保留双 NUL 结尾检查。

- [x] **Step 2: 运行 `pwsh -NoProfile -File .\scripts\test_host.ps1 -Name usb-descriptor`。**

  预期：当前首字符 `7` 与标准 GUID 首字符 `C` 不同，测试失败。

- [x] **Step 3: 将 `s_ms_ext_property.property_data` 改为标准 GUID。**

- [x] **Step 4: 重跑 focused test，预期通过。**

### Task 2: 完整链路文档

**Files:**
- Modify: `docs/wireless_manual.md`

**Interfaces:**
- Consumes: `cmsis_dap_usb.c`、`cmsis_dap.c`、`serial_bridge.c`、`swd_bridge_service.c`、`swd_tunnel.c` 的当前实现。
- Produces: USB 到目标 SWD 再返回 USB 的顺序、队列、超时和时延说明。

- [x] **Step 1: 记录请求路径。**

  覆盖 USB EP5 OUT、六槽内部环、单活动 CMSIS-DAP 状态机、可靠 SWD 帧、
  FLRC TX/RX、从机 SWD 执行器。

- [x] **Step 2: 记录响应路径和可靠性。**

  覆盖请求 ACK、`SWD_BLOCK_RESPONSE`、响应 ACK、重复请求缓存和 Abort。

- [x] **Step 3: 记录基线与优化决策。**

  写明约 4 ms 正常中位时延、约 625 ms 周期长尾、每 32 个 ACK 的触发关系和
  ACK 双方同步切换方案。

### Task 3: ACK 后同步跳频

**Files:**
- Modify: `tests/serial_bridge_hot_path_test.c`
- Modify: `firmware/app/serial_bridge_scheduler.h`
- Modify: `firmware/app/serial_bridge.c`

**Interfaces:**
- Add: `serial_bridge_hop_after_ack(bool requested, uint8_t current_channel, uint8_t next_channel)` 返回是否应在 ACK `TX_DONE` 后切换。

- [x] **Step 1: 写入失败测试。**

  断言未请求时返回 false；下一频道等于当前频道时返回 false；请求且频道不同时
  返回 true。再断言只有 DATA ACK 可以推进周期跳频计数，SWD ACK 不得推进。

- [x] **Step 2: 运行 `pwsh -NoProfile -File .\scripts\test_host.ps1 -Name serial-bridge-hot-path`。**

  预期：辅助接口尚不存在，编译失败。

- [x] **Step 3: 实现最小决策函数并接入 `ack_send()`。**

  ACK 设置 `HOP_VALID` 后保存 `s_channel_after_ack`；仅当辅助函数返回 true 时设置
  `s_channel_switch_after_ack`。沿用现有 `TX_DONE` 分支完成实际切换。

  将 `s_hop_success_count` 的推进条件限制为已确认 DATA；SWD 超时仍沿用
  `frequency_hopping_record_failure()` 和恢复频道选择。

- [x] **Step 4: 重跑 focused test，预期通过。**

### Task 4: 版本、发布和硬件验收

**Files:**
- Modify: `firmware/app/firmware_version.h`
- Modify: `CHANGELOG.md`

**Interfaces:**
- Produces: 当前固件版本 `1.0.18` 和对应 `.dwup`。

- [x] **Step 1: 将版本字段统一更新为 1.0.18/0x0118/1018，并记录 v3 ACK/SWD 调度优化。**

- [ ] **Step 2: 运行 `pwsh -NoProfile -File .\scripts\verify_release.ps1`。**

- [ ] **Step 3: 使用 `tools/daplink_updater.py` 先更新无线从机，再更新无线主机。**

- [ ] **Step 4: 检查两块板的版本、模式、配置和 FLRC 链路状态。**

- [ ] **Step 5: 连续执行 200 次控制命令并记录中位数、P95、最大值和超过 20 ms 的数量。**

- [ ] **Step 6: 重新枚举 USB 后，使用 Keil UV4 对 `G:\Develop\STM32\Project\F10x Project Template\project.uvprojx` 执行实际 Flash Download。**

### Task 5: Keil RDDI-DAP 边界诊断（已清理）

**Files:**
- Modify: `tests/cmsis_dap_protocol_test.c`
- Modify: `tests/cmsis_dap_usb_transport_test.c`
- Modify: `firmware/app/cmsis_dap.h`
- Modify: `firmware/app/cmsis_dap.c`
- Modify: `firmware/usb/cmsis_dap_usb.c`

**Interfaces:**
- Produces: 临时 Vendor Status 扩展；完成根因定位后删除，不作为发布接口。

- [x] **Step 1: 在 1.0.11 上复现 13:22:47 的 Keil 擦除失败并立即读取 Vendor Status v6。**

  结果：`last=0x0`、`invalid=0x0`、`count=0`、`radio_timeouts=0`。请求未进入
  CMSIS-DAP 命令分发层。

- [x] **Step 2: 写入 USB OUT、提交和类生命周期诊断的失败测试。**

  结果：协议测试因诊断接口不存在而编译失败；USB 传输测试因初始化计数为 0
  而断言失败。

- [x] **Step 3: 实现 16 位饱和计数并排除 Vendor Status 查询自身。**

- [x] **Step 4: 运行 `cmsis-dap` 和 `cmsis-dap-usb` focused tests。**

  结果：两项测试通过。

- [x] **Step 5: 刷入 1.0.12 后重新执行 Keil Flash Download，并读取 Vendor Status v7。**

  13:34:06 的结果：基线为 `usb_out/submit=5/5`，失败后为 `262/262`；
  `usb_init/deinit=1/0`、`invalid_count=0`、`radio_timeouts=0`。Keil 的 257 个
  非诊断命令全部进入 CMSIS-DAP 核心，故障位于合法响应或 Flash Algorithm 交互。

### Task 6: CMSIS-DAP 响应追踪（已清理）

**Files:**
- Modify: `tests/cmsis_dap_protocol_test.c`
- Modify: `tests/cmsis_dap_usb_transport_test.c`
- Modify: `firmware/app/cmsis_dap.h`
- Modify: `firmware/app/cmsis_dap.c`
- Modify: `firmware/usb/cmsis_dap_usb.c`

**Interfaces:**
- Produces: 临时 Vendor `0x81` 响应追踪；完成根因定位后删除，不作为发布接口。

- [x] **Step 1: 写入响应摘要、错误快照和 USB IN 边界的失败测试。**

  结果：协议测试因接口不存在而编译失败；USB 传输测试因响应入队计数为 0
  而断言失败。

- [x] **Step 2: 实现 9 槽响应摘要环和 16 位饱和计数。**

- [x] **Step 3: 排除 Vendor `0x80` 和 `0x81` 查询自身。**

- [x] **Step 4: 运行 `cmsis-dap` 和 `cmsis-dap-usb` focused tests。**

  结果：两项测试通过。

- [x] **Step 5: 完成根因定位后删除临时诊断代码，Vendor Status 恢复为 v5。**

- [x] **Step 6: 运行完整发布门禁并刷入 1.0.14，随后执行 Keil Flash Download。**

  结果：1.0.14 软件门禁和双板刷写完成；Keil 在 14:06:35 仍报告
  `Erase Failed!` 和 `RDDI-DAP Error`。

### Task 7: Arm Match Value 完整语义对照和 1.0.15

**Files:**
- Modify: `tests/swd_tunnel_protocol_test.c`
- Modify: `tests/cmsis_dap_protocol_test.c`
- Modify: `firmware/app/swd_tunnel.c`
- Modify: `firmware/app/firmware_version.h`
- Modify: `docs/wireless_manual.md`
- Modify: `CHANGELOG.md`

**Interfaces:**
- Consumes: Arm CMSIS-DAP 2.1.2 `DAP_SWD_Transfer()` 和无线 `SWD_BLOCK`。
- Produces: 保留非零 Match Value 载荷的固件 1.0.15。

- [x] **Step 1: 对照 Arm 官方仓库的完整 Transfer 路径。**

- [x] **Step 2: 确认完成数回归在修复前失败，撤回错误的 Match Value 完成数增量。**

- [x] **Step 3: 确认非零 Match Value 编解码回归在修复前失败，再修复 `SWD_BLOCK` 数据载荷条件。**

- [ ] **Step 4: 运行完整发布门禁，再刷入 D 从机和 E 主机。**

- [ ] **Step 5: 分别验证匹配值正确和故意错误的 AP Match Value，然后重试 Keil Flash Download。**
