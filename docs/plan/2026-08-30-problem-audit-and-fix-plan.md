# 项目问题审计与修复计划（2026-08-30）

本文档是对无线 DAPLink 项目的全量代码审计结论，覆盖三个主题：烧录速度、过度
设计、测试体系。所有结论均给出代码或实测证据，修复按依赖顺序排在文末。
固件应用层约 10.8k 行，审计覆盖了 app 全部模块、USB 传输层、SWD 驱动接口、
构建脚本和测试脚本。

## 1. 现状基线（来自 docs/measurements/）

| 指标 | 数值 | 来源 |
| --- | --- | --- |
| 64 KiB pyOCD 烧录速度 | 约 5.30 kB/s（约 12.4 s） | `2026-08-30-reference-speed-gap.md` |
| FLRC 1.3 Mbps 理论空口速率 | 约 162 kB/s | SX1281 规格 |
| 实际链路利用率 | 约 3% | 上两行相除 |
| 单事务往返 | 约 3.7 ms（ACK 等待 1232.69 µs + 远端执行 1716.81 µs + 响应到 USB IN 734.53 µs） | 同上 |
| SWD_BURST 发送次数 | 0 | 同上 |
| Burst 容量拒绝次数 | 1993 | 同上 |
| RF 重传率 | 0%（链路本身健康，慢在架构） | 同上 |

## 2. 110 字节载荷限制的结论

110 字节限制在软件里真实存在，且不是拍脑袋定的：

| 层 | 常量 | 值 | 位置 |
| --- | --- | --- | --- |
| 芯片驱动 | `SX128X_MAX_PAYLOAD_SIZE` | 127 | `firmware/drivers/radio/sx128x.h:28` |
| 无线协议 | `RADIO_PROTOCOL_PAYLOAD_SIZE` | 110 | `firmware/app/radio_protocol.h:28` |
| SWD 隧道 | `SWD_TUNNEL_MAX_BLOCK_PAYLOAD` | 110 | `firmware/app/swd_tunnel.h:27` |

关系：127（驱动上限）− 17（帧头）= 110。硬件层面分调制模式：

- **FLRC**（DAP 流量实际使用的 profile）：SX1281 硬件单包上限就是 127 字节，
  应用载荷 ≤110 是真实约束。
- **GFSK**：硬件单包上限 255 字节，110 纯属软件选择（驱动统一卡 127）。

对速度的含义：放宽载荷不是主杠杆。FLRC 下压缩帧头最多多 2 个写事务/帧；
GFSK 放宽到 238 字节每帧也只多 ~37 个写。主杠杆是在途流水线深度（见 A1）。
但 110 字节确实解释了 Burst 聚合 100% 失败（两个 16 写 block 编码后
82×2 + 5 = 169 字节 > 110）。

## 3. 问题清单

### A. 烧录慢（主矛盾）

- **A1 SWD 走单帧停等**。`serial_bridge.c` 的 `s_pending` 同时只允许一个
  可靠帧在途；每个 `SWD_BLOCK`（≤21 transfer，约 64 字节烧录数据）付一次
  完整空口往返（约 3.7 ms）。这是 5.3 kB/s 的第一根因。
  证据：`firmware/app/serial_bridge.c` `reliable_queue()`/`swd_command_queue()`。
- **A2 4 槽 `radio_window` 与 SWD 无关**。窗口重传/累计 ACK 机制只服务 CDC
  DATA 帧（`data_frame_transmit()`），SWD 请求完全没用上它。
  证据：`firmware/app/radio_window.c`、`firmware/app/serial_bridge.c`。
- **A3 双重 ACK 纯开销**。SWD 请求 ACK 只确认"帧到达"（之后还要等响应），
  响应还有显式 ACK；每次 ACK 的 TX 转向约 1.2 ms，占往返 1/3。响应 ACK
  丢失反而引入 120 ms 重试窗，需要"下一请求隐式确认"逻辑打补丁
  （`serial_bridge_next_swd_request_confirms_response`）。
  证据：`firmware/app/serial_bridge_scheduler.h:48-60`、
  `firmware/app/serial_bridge.c` ACK 处理分支。
- **A4 Burst 聚合 100% 失败**。110 字节装不下两个 16-transfer 块；实测
  发送 0 次、容量拒绝 1993 次；且 USB 层聚合开关 `CMSIS_DAP_SWD_BURST_ENABLE`
  默认为 0（`firmware/app/cmsis_dap.h:29`），USB 层的聚合代码默认根本没编译。
- **A5 pyOCD 4 包窗口利用不足**。pyOCD 允许最多 `packet_count`（本固件
  广播 4）个未读命令；命令 FIFO 已建，但 burst 只在响应取走的瞬间尝试聚合，
  且受 A4 容量约束永远失败。
  证据：`firmware/app/cmsis_dap.c` `command_queue_pop()`、
  `docs/measurements/2026-08-30-pyocd-outstanding-packets-audit.md`。

### B. 过度设计（约 1200 行可删）

- **B1 `wireless_swd_pipeline.c/h`（167 行）半成品**。2 槽状态表建好但从未
  接入主路径，却已编进固件（CMakeLists.txt 第 54 行）。
  证据：`docs/measurements/2026-08-30-host-pipeline-state-module.md` 自述
  "尚未接入 serial_bridge 主路径"。
- **B2 SWD_BURST 全链路约 700 行死代码**，横跨 4 个模块：
  `swd_tunnel` 编解码约 270 行、`swd_bridge_service` 的 burst 执行/响应、
  `cmsis_dap` 的聚合/解析/响应重组约 300 行、`serial_bridge` 的 burst 分支。
  在 A4 证实 0 次成功；窗口流水线方案
  （`docs/superpowers/plans/2026-08-30-wireless-transaction-pipeline-v1.md`）
  让它彻底多余。
- **B3 `serial_bridge_scheduler.h` 伪配置层（117 行）**。所有"策略函数"已
  坍缩为常量：`serial_bridge_request_ack_required()` 恒返回 true、
  `serial_bridge_idle_recovery_channel()` 忽略 scan 参数、
  `serial_bridge_ack_compact()` 就是 `!auto_rate`。纯间接层。
- **B4 ACK 双布局**（17 B 完整 / 8 B 紧凑）+ AUTO/FIXED 分支散布在
  `ack_send()`、`frame_deliver()`、`radio_protocol.c` 三处；AUTO 还是默认
  速率模式（`device_config.h:30`）。
- **B5 超时层级 6 层**：12 ms ACK 抖动、120 ms ACK 超时、500 ms 响应超时、
  2500 ms 执行预算、4000 ms CMSIS-DAP 超时、8 ms jitter。多数是停等架构
  需要的补偿，流水线化后可收敛为 2-3 层。
  证据：`firmware/app/serial_bridge.c:42-54`。
- **B6 废弃的 HOP_SWITCH/HOP_CONFIRM 帧类型**。v2 已把跳频目标合并进 ACK，
  文档明说"不再是运行时接受的控制路径"，但类型常量、处理分支、重复帧分支
  全部保留。
  证据：`firmware/app/radio_protocol.h:46-47`、`firmware/app/serial_bridge.c`。

### C. 测试体系（5271 行测试 vs 10781 行固件）

- **C1 字符串匹配伪测试**。`tests/serial_bridge_reliability_test.py` 与
  `tests/serial_bridge_turnaround_test.py` 用 `source.index()` 在
  serial_bridge.c 源码里查找特定文本片段做 assert——不验证任何运行时行为，
  任何重构必假红。
- **C2 测常量的测试**。`tests/serial_bridge_hot_path_test.c` 全部 74 行在
  assert 伪配置层（B3）的恒定返回值。
- **C3 当前测试门禁已红**。`serial_bridge_hot_path_test.c:34` 断言
  `serial_bridge_ack_turnaround_delay_us() == 50U`，但
  `serial_bridge_scheduler.h:10` 默认值是 0U（50 µs 实验值没有同步回
  头文件），`scripts/test_host.ps1 -Name serial-bridge-hot-path` 实测失败。
  说明 2026-08-30 的"门禁全绿"记录与当前代码不一致。
- **C4 文档/代码漂移**。`docs/wireless_manual.md` 宣称协议 v4
  （`RADIO_PROTOCOL_VERSION 4U`、帧头偏移 2 为 `04`），但代码是
  `#define RADIO_PROTOCOL_VERSION 3U`（`radio_protocol.h:29`）。两端从同一
  代码构建所以功能一致，但文档与实现不同步。
- **C5 注册方式**。27 个 host 测试手工注册在 `scripts/test_host.ps1`
  （无 CTest），其中含 `cmake_structure_test.py`、`build_factory_hex_test.py`
  这类仓库结构/构建产物检查。
- **C6 有效测试**。`cmsis_dap_protocol_test.c`（722 行）、
  `swd_tunnel_protocol_test.c`（665 行）、`swd_bridge_service_test.c`、
  `radio_protocol_test.c` 等白盒测试有真实价值，但测的是停等语义，
  流水线化后需同步改写。

## 4. 修复计划（按依赖顺序）

### 阶段 1：删死代码（对应 B1/B2/B6/B3 + C1/C2）

| 动作 | 涉及文件 |
| --- | --- |
| 删除 `wireless_swd_pipeline.c/h` | 文件本身、`CMakeLists.txt` |
| 删除 SWD_BURST 全链路 | `swd_tunnel.h/.c`、`swd_bridge_service.h/.c`、`serial_bridge.h/.c`、`cmsis_dap.h/.c`、`cmsis_dap_usb.c`、`radio_protocol.h` 帧类型、`dap_diagnostics.h` burst 事件 |
| 删除 HOP_SWITCH/HOP_CONFIRM 帧类型与分支 | `radio_protocol.h`、`serial_bridge.c` |
| 删除伪配置层（并入修复三的语义变化一起做） | `serial_bridge_scheduler.h` |
| 删除伪测试 | `serial_bridge_reliability_test.py`、`serial_bridge_turnaround_test.py`、`serial_bridge_hot_path_test.c` 及 `test_host.ps1` 注册项 |
| 更新受影响的有效测试 | `radio_protocol_test.c`、`swd_tunnel_protocol_test.c`、`cmsis_dap_protocol_test.c`、`swd_bridge_service_test.c`、`cmsis_dap_usb_transport_test.c` |

预期：固件约 -900 行；burst 相关诊断计数（容量拒绝 1993 的来源）随之消失。

### 阶段 2：CMSIS-DAP 核心流水化（对应 A5）

- 命令 FIFO（现有 `s_command_queue`，4 槽）+ 响应 FIFO 各 4 槽，替代
  "单响应 + burst 多响应数组"的混合结构。
- 解除"响应取走瞬间才 pop 队列"的时序约束：USB 提交、核心推进、响应交付
  三者解耦，对齐 pyOCD 的 4 包窗口。
- 响应按命令顺序交付 USB（现有响应环已按序，FIFO 对齐即可）。

### 阶段 3：SWD 接入 radio_window（对应 A1/A2/A3，提速主项）

- 主机端：SWD 请求帧（`SWD_COMMAND`/`SWD_BLOCK`）从 `s_pending` 单槽迁入
  host→slave 的 4 槽窗口，按帧类型与 DATA 分流投递；`s_pending` 仅保留
  控制帧（SESSION_START、PROFILE_SWITCH、SWD_ABORT 等）。
- 取消 SWD 请求 ACK 与响应显式 ACK：窗口累计 ACK + 逐槽重传覆盖两个方向。
- 响应帧走 slave→host 窗口；`serial_bridge_next_swd_request_confirms_response`
  隐式确认补丁、重复帧 key 抑制中针对 SWD 的部分随之删除。
- 窗口重传超时按帧类型区分（SWD 短超时延续 12 ms 量级，DATA 保持 120 ms）。
- 预期：4 块在途 × 21 transfer，空口利用率从 ~3% 提到 >30%；64 KiB 实测
  目标 ≥ 4×（≥ 20 kB/s），后续再评估 GFSK 放宽载荷。

### 阶段 4：从机端流水（对应 A1 的从机半边）

- 请求 FIFO 深度 2→4（现有 `SWD_BRIDGE_PENDING_BLOCK_QUEUE_SIZE`），
  按序执行，响应按序入从机发送窗口。
- Abort 语义保留：SWD_ABORT 帧取消当前执行与未执行队列。

### 阶段 5：测试与门禁修复（对应 C1-C6）

- 删除 C1/C2 伪测试（并入阶段 1）。
- 协议测试改写为窗口语义：乱序到达、重复请求、Abort、超时重传。
- 修复 50 µs/0 µs 不一致（C3）后维持"全绿才算过"的门禁。
- 评估把 test_host.ps1 的测试矩阵迁入 CTest（低优先级）。

### 阶段 6：文档与实测

- `docs/wireless_manual.md`：协议版本字段与 `RADIO_PROTOCOL_VERSION` 对齐
  （C4）；删除 SWD_BURST 章节；补窗口 SWD 新语义。
- 主从两端同刷（协议语义变化要求），复测 64 KiB 并记录到
  `docs/measurements/`。

## 5. 关键决策与风险

1. **协议版本升级**：删除 `SWD_BURST`/`HOP_SWITCH`/`HOP_CONFIRM` 帧类型、
   取消 SWD 请求 ACK 属于线格式语义变化，两端必须同刷。这符合
   `wireless_manual.md` 已写明的"不保留旧版本兼容路径"原则；同时把
   `RADIO_PROTOCOL_VERSION` 与文档一起对齐。
2. **有线模式不动**：`DEVICE_MODE_WIRED` 路径不经过无线窗口，保持现状。
3. **AUTO/自适应与跳频保留**：它们有真实场景（干扰环境），但 ACK 双布局
   随窗口化重新评估（窗口 ACK 本身需要完整布局，紧凑布局可能失去意义）。
4. **验收标准**：固件 Release 构建零警告；host 测试全绿；双板实测 64 KiB
   速度对比基线并记录；Keil/pyOCD 烧录流程不回退。
5. **不动手前先记录**：本文档即"先记录"产物；后续每个阶段的实际 diff
   在对应提交中体现。

## 6. 实施状态（2026-08-30 更新）

### 阶段 1：删死代码 —— 已完成

- `wireless_swd_pipeline.c/h` 已删除（CMake 同步）。
- SWD_BURST 全链路已删除：`swd_tunnel` 编解码、`swd_bridge_service` burst 执行、
  `cmsis_dap` 聚合/重组、`cmsis_dap_usb` USB 层聚合、`RADIO_FRAME_SWD_BURST*`
  帧类型、`dap_diagnostics` burst 统计与诊断页 3、`tools/dap_diagnostics.py`
  对应字段。
- `HOP_SWITCH`/`HOP_CONFIRM` 帧类型及处理分支已删除（计划跳频仍经 ACK 携带）。
- 伪测试已删除：`serial_bridge_reliability_test.py`、
  `serial_bridge_turnaround_test.py`（源码字符串匹配）、
  `serial_bridge_hot_path_test.c`（常量断言）及其 `test_host.ps1` 注册项。
- 净变化：固件 + 测试合计约 -1500 行。

### 阶段 2+3：交替流水线 —— 已完成（1.0.49-1.0.51）

实现采用比原计划更小的改动面（不引入第二套窗口 ACK）：

- `SERIAL_BRIDGE_SWD_REQUEST_ACK_ENABLE` 默认 0：SWD 请求不再发送独立请求
  ACK，由端到端响应确认；请求丢失以指数退避重传同一帧（12 至 768 ms；
  帧键去重保证从机不重复执行写操作），不再按固定次数杀射频——擦除/算法
  轮询的长执行属合法行为，事务上限由 CMSIS-DAP 4 s 超时兜底。
  1.0.50 修复 1.0.49 的两处缺陷：CMSIS-DAP 槽位共享暂存区导致的读映射
  错乱、固定 12 ms 重传误杀长操作。
- `serial_bridge` 新增 SWD 请求队列（`s_swd_queue`，后扩至 8 槽）：同一时刻
  只有一个请求在途（交替模式），响应到达即推进队头；控制帧仍走单槽可靠路径。
- `swd_bridge_service` 主机侧新增按序期望事务 FIFO + 响应 FIFO（8 深），
  支持流水线响应匹配；从机侧执行路径未改动（交替模式下天然单请求在途）。
- `cmsis_dap` 重写为槽位流水线：Transfer 类命令最多 8 个在途（每槽独立
  transfer 表）；控制命令为屏障；Immediate 命令提交即完成；
  `DAP_ExecuteCommands` 子命令按序拼装，子槽内部消化不占 USB 响应。
- 流水线深度全线 4→8（CMSIS-DAP 对外窗口、命令 FIFO、USB 请求/响应环、
  桥接 FIFO、射频请求队列）。
- 响应 ACK 在主机仍有排队请求时跳过（下一请求构成隐式确认）。
- `RADIO_PROTOCOL_VERSION` 对齐为 `4U`；固件版本 1.0.52。
- 线格式变化：无 SWD_BURST 帧；SWD 请求无请求 ACK。两端必须同刷。

### 阶段 1 补充：去防御性编程（1.0.51）

- 删除 `serial_bridge_scheduler.h` 伪配置层（全部为常量返回值的间接层，
  语义内联进 `serial_bridge.c`）。
- 删除 `swd_queued`/`single_swd` 诊断桩与 `add_saturated` 死代码。
- 删除内部 API 的防御性 NULL 检查（frequency_hopping、radio_window、
  swd_bridge_service）；USB/射频帧解析的信任边界校验全部保留。

### 阶段 4：从机侧 —— 无需改动

交替模式下从机任意时刻最多只有一个请求在途，现有块执行预算（1600 µs）、
重复帧缓存重发（`swd_bridge_service_repeat_request`）和 2 深块队列已覆盖。

### 阶段 5：测试 —— 已完成

- 主机套件 31 项全绿（`scripts/test_host.ps1` 需用 pwsh 运行；powershell 5.1
  在 Git Bash 下输出流异常）。
- Python 套件 25 项全绿；`pyocd_flash_script_test` 增加解码容错（PS 5.1 中文
  报错为 GBK，UTF-8 解码曾使子进程捕获崩溃）。
- 协议测试更新：cmsis_dap 流水线语义、radio 帧类型、swd_tunnel/bridge 删
  burst、usb 传输删聚合块并适配深度 8。

### 阶段 6：实测 —— 进行中（见第 7 节）

构建产物：Release text 49300 → diag 版 50044，bss 31692 → 35820（深度 8 +
诊断计数器）。实测见下节。

## 7. 瓶颈定位实测（2026-08-30 深夜，1.0.52 诊断固件）

### 实验矩阵（64 KiB，chip erase，GFSK2M）

| 配置 | 速度 | 重传率 | 请求环深度均值 | radio RTT 均值 |
| --- | --- | --- | --- | --- |
| FLRC1M3 + 1MHz + 深度4（1.0.49 前基线） | 7.21 kB/s | 0.5% | - | - |
| GFSK2M + 1MHz + 深度8（1.0.52） | 9.48-10.62 kB/s | 0.5% | 1.02（max 3） | 12.4 ms |
| GFSK2M + 4MHz + 深度8 | 5.67 kB/s | 5.1% | 1.02 | 12.9 ms |
| GFSK2M + 2MHz + pyOCD deferred | 7.22 kB/s | 2.1% | 1.02 | 12.4 ms |

### 关键结论

1. **4 MHz SWD 在当前目标接线/时钟下不稳定**：重传率 10 倍恶化、速度反而
   减半。瓶颈在目标板信号完整性，固件无能为力；安全点是 1-2 MHz。
2. **pyOCD 默认同步模式是主要的外部瓶颈**：`cmsis_dap.deferred_transfers`
   默认关闭（cmsis_dap_probe.py:280 读会话选项，默认 None 即 False），
   每条 CMSIS-DAP 命令都是发送后等响应。请求环深度均值 1.02、最大 3，
   说明 pyOCD 从未用满 8 深度窗口。开启 deferred 后环深仍为 1.02，说明
   pyOCD-libusb-Windows 的每命令写入延迟（1.5-4 ms 量级）独立于 deferred
   设置存在——必须减少命令数（更大 CMSIS 包）才能绕开。
3. **射频单块时间约 1.55 ms**（RTT 均值 12.4 ms 除以深度 8，与模型吻合：
   请求帧 0.43 + SWD 执行 0.8@1MHz + 响应帧 0.17 + 转向 0.2）。数据段
   理论天花板约 40 kB/s；重传退避（12-768 ms 阶梯）以 2-5% 触发率显著
   抬高 RTT 均值。
4. **命令构成双峰**：1234 个 13-16 项数据块（可流水）+ 1279 个 2 项命令
   （约 20 个/页的算法调用序列，依赖链不可流水，每个付全往返）。
5. 逐写 RDBUFF 检查经核实并不存在（中间写的检查标志会被下一事务覆盖，
   仅块尾一次）——1.0.50 分析中的"写事务翻倍"判断有误，已澄清。

### 48 kB/s 路径修订

pyOCD 每命令 1.5-4 ms 的串行成本与射频单块 1.55 ms 已经互相平衡，
两侧必须同时改进：

1. **CMSIS-DAP PACKET_SIZE 512**（v2 允许 FS USB 多包重组）：pyOCD 命令数
   减少 4-8 倍，直接摊薄其每命令成本；固件需 OUT 重组 + IN 分段 +
   命令缓冲扩容（RAM 预算紧张，需评估）。
2. **射频载荷 238B（仅 GFSK）+ 两个 USB 包合并为 32 写块**：数据段帧数
   减半，天花板约 80 kB/s。需 profile 感知的 TX 上限保护（FLRC 硬件 127）。
3. pyOCD 命令数不变的替代方案：自研 C/Python 烧录工具做真 8 深异步流水，
   先测出射频侧真实天花板，作为 1/2 两项的验收基准。
4. 目标板 SWD 布线改善后重试 2-4 MHz。

### 诊断流程（复核用）

主机板刷 build/gcc/diag-1051/daplink_wireless.dwup（diag 构建与 release
同码 + 计数器，与从机线兼容）：

    python tools/dap_diagnostics.py reset
    pyocd flash --erase chip --base-address 0x08000000 -t stm32f103c8 test_64k.bin
    python tools/dap_diagnostics.py dump

若设备停在 DFU 模式且多板同时在线，updater 会拒绝多设备；可按 USB path
逐台刷写（dfu-util --device 28e9:1291 --path <path> -a 0 -D image.bin）。
