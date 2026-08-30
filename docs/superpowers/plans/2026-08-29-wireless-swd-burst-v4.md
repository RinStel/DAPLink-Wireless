# 无线 SWD Burst v4 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 利用已实测存在的 CMSIS-DAP USB 请求流水线，在不改变 USB 命令边界和目标 SWD 语义的条件下，减少无线 SWD 往返次数，使 64 KiB pyOCD 烧录体验接近有线 CMSIS-DAP。

**Architecture:** Dongle 仅聚合请求环中已经到达的 `DAP_Transfer (0x05)` 和 `DAP_TransferBlock (0x06)`，不等待后续请求。每个 USB 命令解析为独立 SWD 子块；无线 v4 Burst 帧保存子块边界，Remote 逐块调用现有 SWD 执行器并独立完成 posted-read、`DP_RDBUFF` 和写后检查。Burst 响应保存各子块响应边界，Dongle 按原顺序恢复成多个 USB IN 响应。

**Tech Stack:** C11、GD32F303、CMSIS-DAP v2 Bulk、SX1281 FLRC、无线协议 v4、CMake、PowerShell、Python/PyUSB、pyOCD、Keil UV4。

**Spec:** `docs/measurements/2026-08-29-pyocd-64k-request-ring.json` 和用户于 2026-08-29 批准的 burst 方向。

## Global Constraints

- `RADIO_PROTOCOL_VERSION` 从 `3U` 递增到 `4U`；不保留 v3/v2/v1 无线兼容路径，主从机必须同时升级。
- 不重新声明 CMSIS-DAP Atomic Commands；`DAP_Info(0xF0)` 默认保持 `0x01`。
- 不修改 USB VID/PID、接口号、EP5 Bulk 端点和 64 字节最大包长。
- 不修改 FLRC profile、跳频、ACK 语义、SWD 时钟和目标 SWD 位时序。
- 每个子块必须保持现有独立 SWD 收尾语义；不得直接拼接 transfer 数组后一次执行。
- 聚合不得等待新请求，不增加 flush 定时器；无第二条可聚合请求时立即走现有单发路径。
- 请求帧和最坏响应帧都必须在现有 110 字节应用载荷内。
- Abort、Delay、Reset、Connect、Disconnect、Configure、Sequence、Atomic、Vendor 命令均为 burst 边界。
- Burst 解析或无线事务失败时必须回退到原子块顺序单发；不得重复已确认完成的子块。
- 诊断字段继续受 `CMSIS_DAP_DIAGNOSTICS_ENABLE` 条件编译控制；关闭时不得增加热路径统计开销。
- 软件门禁、双板升级、pyOCD 吞吐和 Keil Flash Download 分别记录。
- 不重置、清理、提交或推送现有工作树。

---

### Task 1: 定义 v4 Burst 编解码器和容量模型

**Files:**
- Modify: `firmware/app/radio_protocol.h`
- Modify: `firmware/app/radio_protocol.c`
- Modify: `firmware/app/swd_tunnel.h`
- Modify: `firmware/app/swd_tunnel.c`
- Test: `tests/radio_protocol_test.c`
- Test: `tests/swd_tunnel_protocol_test.c`

**Interfaces:**
- Add: `RADIO_FRAME_SWD_BURST`
- Add: `RADIO_FRAME_SWD_BURST_RESPONSE`
- Add: `SWD_TUNNEL_BURST_MAX_BLOCKS 3U`
- Add: `swd_tunnel_burst_t`，包含 `count` 和最多三个独立 `swd_tunnel_block_t`。
- Add: `swd_tunnel_burst_response_t`，包含独立子响应的 `completed`、`ack` 和读数据。
- Add: `swd_tunnel_burst_encode/decode()` 和 `swd_tunnel_burst_response_encode/decode()`。
- Add: `swd_tunnel_block_encoded_lengths()`，同时计算请求长度和最坏响应长度。

- [ ] **Step 1: 写入 Burst 请求编解码失败测试**

  构造两个独立子块：第一个包含 DP read，第二个包含 AP write。断言编码后保留两个 transaction ID、两个 transfer count、各自 request/data，并能无损解码。

- [ ] **Step 2: 运行 `pwsh -NoProfile -File .\scripts\test_host.ps1 -Name swd-tunnel`，确认因 Burst 接口不存在而失败**

- [ ] **Step 3: 实现长度预计算和 Burst 请求编解码**

  帧内格式使用版本化、长度前缀子块：

  ```text
  byte 0      operation = SWD_TUNNEL_OP_BURST
  byte 1      burst transaction ID
  byte 2      sub-block count，范围 2..3
  byte 3..    repeated { uint8_t block_length; encoded SWD_BLOCK body }
  ```

  `block_length` 不包含自身；总长度不得超过 `SWD_TUNNEL_MAX_BLOCK_PAYLOAD=110`。

- [ ] **Step 4: 写入 Burst 响应编解码失败测试**

  覆盖两个成功读响应、第二块 FAULT、子响应数量不一致、截断数据和超过 110 字节的最坏读响应。

- [ ] **Step 5: 实现 Burst 响应编解码**

  ```text
  byte 0      operation = SWD_TUNNEL_OP_BURST
  byte 1      burst transaction ID
  byte 2      sub-response count
  byte 3..    repeated { uint8_t response_length; encoded block response body }
  ```

  错误 ACK 是合法子响应；编解码器不得因为某个子块返回 WAIT/FAULT/MISMATCH 而丢弃后续响应。

- [ ] **Step 6: 在 `radio_protocol` 中分配 v4 帧类型并更新合法范围测试**

- [ ] **Step 7: 重跑 `radio-protocol` 和 `swd-tunnel` focused tests，确认通过**

---

### Task 2: Remote 顺序执行独立子块

**Files:**
- Modify: `firmware/app/swd_bridge_service.h`
- Modify: `firmware/app/swd_bridge_service.c`
- Modify: `firmware/app/swd_tunnel.h`
- Modify: `firmware/app/swd_tunnel.c`
- Test: `tests/swd_bridge_service_test.c`
- Test: `tests/swd_tunnel_protocol_test.c`

**Interfaces:**
- Add: `swd_bridge_service_wireless_burst_request(const uint8_t *payload, uint8_t length)`。
- Add: `swd_bridge_service_reply_is_burst(void)`。
- Add: Remote burst 状态：当前子块索引、子块总数、已完成子响应和最终编码缓冲区。

- [ ] **Step 1: 写入 Remote 两子块顺序执行失败测试**

  提交两个子块，断言第二块只能在第一块 `swd_tunnel_response_take()` 完成后提交；记录两次独立 executor 调用，而不是一次合并调用。

- [ ] **Step 2: 运行 `swd-bridge-service` focused test，确认失败原因是 Burst 接口不存在**

- [ ] **Step 3: 实现 Remote Burst 状态机**

  Remote 解码全部子块并验证请求/最坏响应总长后，依次调用现有 `swd_tunnel_submit_block()`。每个子块完成后保存其原始 block response，再推进下一块。所有子块完成后才生成一个 Burst Response。

- [ ] **Step 4: 保持每块独立 SWD 收尾**

  不修改 `transfer_async_finish()`、AP posted-read、`DP_RDBUFF`、WAIT 重试、Match Value 或写后检查代码。每个子块通过一次完整的 `swd_tunnel_submit_block()` 生命周期执行。

- [ ] **Step 5: 实现取消行为**

  收到 Abort 时取消当前子块，清除尚未执行的子块，并生成可被 Dongle 识别的 Burst 失败结果；不得执行后续子块。

- [ ] **Step 6: 增加 FAULT/WAIT/MISMATCH 和取消测试**

  断言合法错误子响应仍保留；取消后第二块不执行；重复 Burst 请求由现有可靠缓存返回相同响应，不重复写目标。

- [ ] **Step 7: 重跑 `swd-bridge-service` 和 `swd-tunnel` tests，确认通过**

---

### Task 3: Dongle 聚合已排队请求并恢复 USB 边界

**Files:**
- Modify: `firmware/app/cmsis_dap.h`
- Modify: `firmware/app/cmsis_dap.c`
- Modify: `firmware/usb/cmsis_dap_usb.c`
- Modify: `firmware/app/serial_bridge.h`
- Modify: `firmware/app/serial_bridge.c`
- Modify: `firmware/app/swd_bridge_service.h`
- Modify: `firmware/app/swd_bridge_service.c`
- Test: `tests/cmsis_dap_protocol_test.c`
- Test: `tests/cmsis_dap_usb_transport_test.c`
- Test: `tests/swd_bridge_service_test.c`

**Interfaces:**
- Add: `cmsis_dap_burst_eligible(const uint8_t *request, uint8_t length)`。
- Add: `cmsis_dap_submit_burst(const uint8_t *const requests[], const uint8_t lengths[], uint8_t count)`。
- Add: `cmsis_dap_response_pending_count()`，用于 USB 层按序取得多个响应。
- Add: `serial_bridge_swd_burst(...)`，提交 2..3 个独立 SWD 子块。
- Add: `swd_bridge_service_begin_burst(...)`。

- [ ] **Step 1: 写入请求白名单和边界失败测试**

  `0x05/0x06` 返回 eligible；`0x00/0x01/0x02/0x03/0x04/0x07/0x08/0x09/0x0A/0x10/0x11/0x12/0x13/0x1D/0x7E/0x7F/0x80/0x81` 返回 false。截断命令、零 count 和不能解析的请求返回 false。

- [ ] **Step 2: 写入 USB 无等待聚合失败测试**

  - 环中只有一条 eligible 请求：本轮立即提交普通命令。
  - 环中已有两条 eligible 请求且双向长度可容纳：一次提交 Burst。
  - 第二条是边界命令：第一条普通提交，边界命令保持排队。
  - 三条中第三条装不下：只聚合前两条，第三条保持排队。

- [ ] **Step 3: 运行 `cmsis-dap` 和 `cmsis-dap-usb` tests，确认缺少 Burst 接口导致失败**

- [ ] **Step 4: 将请求解析与执行准备分离**

  复用现有 `transfer_parse()`/`transfer_block_parse()` 语义，生成每个子命令独立的 SWD block 描述和 USB 响应描述。不得覆盖全局 `s_transfers` 后再解析下一条；为最多三个子块分配固定数组。

- [ ] **Step 5: 实现双向容量预算**

  仅当下列条件同时成立时追加子块：

  ```text
  encoded_burst_request_length  <= 110
  worst_case_burst_response_len <= 110
  sub_block_count               <= 3
  ```

  预算使用实际 read count；写数据只计入请求，读数据只计入响应。

- [ ] **Step 6: 实现 Burst 响应拆分**

  Dongle 验证 burst ID、子响应数量和每个 transaction ID，然后复用现有 `transfer_complete()` 规则生成多个独立 CMSIS-DAP 响应。USB 层逐条写入响应环，保持原请求顺序和原响应长度。

- [ ] **Step 7: 实现安全回退**

  Burst 在发送前失败时不消费请求环，改为提交第一条普通请求。Burst 已发送但响应解析失败或超时时，不得盲目重放整个 Burst；先返回对应 DAP transfer error，保留诊断证据，避免重复 Flash 写。只有能证明 Remote 未接收请求时才允许按原顺序单发。

- [ ] **Step 8: 增加响应顺序、错误子响应、超时和 Abort 测试**

- [ ] **Step 9: 重跑三个 focused tests，确认通过**

---

### Task 4: 接入 v4 无线可靠传输

**Files:**
- Modify: `firmware/app/serial_bridge.c`
- Modify: `firmware/app/serial_bridge_scheduler.h`
- Modify: `firmware/app/radio_protocol.c`
- Modify: `firmware/app/swd_bridge_service.c`
- Test: `tests/serial_bridge_hot_path_test.c`
- Test: `tests/serial_bridge_reliability_test.py`

**Interfaces:**
- `RADIO_FRAME_SWD_BURST` 使用现有可靠请求 ACK。
- `RADIO_FRAME_SWD_BURST_RESPONSE` 使用现有可靠响应 ACK、重复请求缓存和“下一请求隐式确认上一响应”规则。

- [ ] **Step 1: 写入帧调度失败测试**

  断言 Burst 请求需要到达 ACK；Burst Response 是 SWD 最终响应；下一条 SWD/Burst 请求可隐式确认上一 Burst Response；DATA 周期跳频计数不被 Burst ACK 推进。

- [ ] **Step 2: 扩展帧分发白名单和 `frame_deliver()`**

  Slave 将 Burst 请求交给 `swd_bridge_service_wireless_burst_request()`；Host 将 Burst Response 交给对应解码接口。

- [ ] **Step 3: 扩展可靠缓存与重复请求处理**

  Remote 收到相同 session/sequence/type 的 Burst 重传时只重发缓存响应，不重复执行任一子块。

- [ ] **Step 4: 扩展超时和取消路径**

  沿用 CMSIS-DAP 4000 ms 总超时和 SWD response timeout；统计 Burst 请求重传、响应重传、解析错误和取消次数。

- [ ] **Step 5: 重跑 hot-path、reliability、radio-protocol 和 SWD service tests**

---

### Task 5: 诊断指标和采集工具

**Files:**
- Modify: `firmware/app/dap_diagnostics.h`
- Modify: `firmware/app/dap_diagnostics.c`
- Modify: `tools/dap_diagnostics.py`
- Test: `tests/cmsis_dap_protocol_test.c`

**Interfaces:**
- Add: `burst_tx_count`
- Add: `burst_command_total`
- Add: `burst_max_commands`
- Add: `burst_histogram[3]`，对应 1/2/3 子块；1 表示容量判断后回退单发。
- Add: `single_swd_tx_count`
- Add: `burst_request_bytes`、`burst_response_bytes`
- Add: `burst_fallback_count`、`burst_parse_error_count`

- [ ] **Step 1: 写入统计清零、饱和累计和分页读取失败测试**

- [ ] **Step 2: 在 `CMSIS_DAP_DIAGNOSTICS_ENABLE` 下接入聚合、单发和错误 hook**

- [ ] **Step 3: 扩展 JSON 输出和派生指标**

  输出平均每 Burst 子块数、无线事务减少比例、Burst/单发比例、重传率和原有四段平均时延。

- [ ] **Step 4: 验证关闭诊断宏时 Release text/bss 不包含统计对象和 hook 调用**

  比较普通 Release map、`size` 和反汇编符号；允许协议实现自身增加空间，但不得出现 `dap_diagnostics_*` 运行时符号。

---

### Task 6: 版本、文档和软件门禁

**Files:**
- Modify: `firmware/app/firmware_version.h`
- Modify: `CHANGELOG.md`
- Modify: `docs/wireless_manual.md`
- Modify: `docs/project_manual.md`
- Modify: `dependencies.lock.json`

**Interfaces:**
- Produces: 固件 `1.0.36`、无线协议 `4U`、普通 Release 和诊断 Release `.dwup`。

- [ ] **Step 1: 更新协议文档**

  记录 Burst 请求/响应格式、容量预算、子块独立执行、可靠缓存、Abort、失败响应和单发回退条件。

- [ ] **Step 2: 将版本统一递增到 `1.0.36 / 0x0136 / 1036`，无线协议递增到 `4U`**

- [ ] **Step 3: 运行完整主机测试**

  ```powershell
  pwsh -NoProfile -File .\scripts\test_host.ps1 -Name all
  ```

- [ ] **Step 4: 运行普通 Release 门禁**

  ```powershell
  pwsh -NoProfile -File .\scripts\verify_release.ps1
  ```

- [ ] **Step 5: 生成诊断 Release**

  复用已验证的 Release 工具链缓存，重新配置：

  ```powershell
  cmake -S . -B build/gcc/release -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMSIS_DAP_DIAGNOSTICS=ON
  cmake --build build/gcc/release
  ```

- [ ] **Step 6: 运行 `git diff --check` 和 CSTE-ZH 检查**

---

### Task 7: 双板升级和实机验收

**Files:**
- Create: `docs/measurements/2026-08-29-pyocd-64k-burst-v1.json`
- Modify: `docs/superpowers/notes/2026-08-29-keil-cmsis-dap-conformance-handoff.md`

**Interfaces:**
- Consumes: `build/gcc/release/daplink_wireless.dwup`
- Produces: pyOCD、Keil 和无线诊断的实机证据。

- [ ] **Step 1: 确认 D/E 角色和旧版本**

  `D:\CONFIG.TXT` 必须是 `WIRELESS_SLAVE`；`E:\CONFIG.TXT` 必须是 `WIRELESS_HOST`。

- [ ] **Step 2: 使用项目更新器先升级从机 D，再升级主机 E**

  ```powershell
  python tools/daplink_updater.py build/gcc/release/daplink_wireless.dwup --volume D:\
  python tools/daplink_updater.py build/gcc/release/daplink_wireless.dwup --volume E:\
  ```

- [ ] **Step 3: 读取两块板状态页**

  两块板均必须报告 `FIRMWARE=1.0.36`、`CONFIG_STATUS=OK`，角色保持不变。

- [ ] **Step 4: 清零统计并执行同一 64 KiB pyOCD 基线**

  ```powershell
  python tools/dap_diagnostics.py reset
  pyocd flash --erase chip --base-address 0x08000000 `
    -t stm32f103c8 G:\Develop\DAPLink_Wireless\test_64k.bin
  python tools/dap_diagnostics.py dump
  ```

- [ ] **Step 5: 保存测量 JSON并与 1.0.34 的 `5.30 kB/s` 对比**

  必须记录 Burst 次数、平均子块数、单发次数、RF 重传率、四段时延和总吞吐。若平均 Burst 子块数小于 1.5，不得把收益不足归因于射频。

- [ ] **Step 6: 连续执行三次 64 KiB 测量**

  报告中位吞吐、最小/最大吞吐和每次重传率。单次结果不得作为最终性能结论。

- [ ] **Step 7: 执行 Keil F10x Flash Download**

  必须得到：

  ```text
  Erase Done.
  Programming Done.
  Verify OK.
  ```

- [ ] **Step 8: 执行 pyOCD 基本读写和 Abort 验收**

  读取 DP IDR、AHB-AP IDR、CPUID；中断一次长操作后重新连接并成功读取 CPUID。

- [ ] **Step 9: 记录验收边界**

  仅当三次 pyOCD、Keil、重连和版本读取全部通过时，才能宣称 Burst v1 实机通过。若吞吐未提升，保留统计并回退 v3 单发，不叠加 ACK/profile/时钟改动。
