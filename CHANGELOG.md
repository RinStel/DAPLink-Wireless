# Changelog

## 1.0.26 - 2026-08-29

- 放宽官方 `ExecuteCommands` 聚合子命令数量上限至请求包可容纳的 32 条，避免 Keil 批量编程请求被错误判为无效。

## 1.0.25 - 2026-08-29

- 修正官方 `ExecuteCommands` 子命令长度解析，覆盖 `DAP_HostStatus`、`DAP_Delay` 和 `DAP_SWD_Configure`，避免 Keil 批量编程请求错位。

## 1.0.24 - 2026-08-29

- 按 Arm CMSIS-DAP 官方语义实现 `DAP_ExecuteCommands (0x7F)` 聚合处理，并在 USB 传输层支持 `DAP_QueueCommands (0x7E)` 到 Execute 的转换。
- 保留并优先优化 FLRC 无线链路的紧凑 ACK、隐式响应确认和快速重试路径。

## 1.0.23 - 2026-08-29

- 恢复 v3 无线通信链路修复：固定 FLRC 使用紧凑 ACK、跳过无用 packet status，下一条 SWD 请求隐式确认上一响应，SWD 请求 ACK 使用 12–19 ms 快速重试窗口。
- 新增官方 Arm CMSIS-DAP 2.1.2 差分验证基准；本版本不增加私有 Keil 诊断协议。

## 1.0.21 - 2026-08-29

- 恢复 1.0.16 的无线可靠传输时序：SWD 请求 ACK 使用 600–640 ms 窗口，上一条 SWD 响应仅由显式 ACK 释放。
- 同时保留 1.0.20 恢复的完整 ACK 和 `GET_PACKET_STATUS`，用于验证 Keil 连续 Flash Download 的最后已通过行为集合。

## 1.0.20 - 2026-08-29

- 固定 FLRC profile 恢复 17 字节完整 ACK，并恢复每个接收包的 `GET_PACKET_STATUS`；撤销 1.0.17 中尚未通过 Keil 连续 Flash Download 验收的 RX 热路径。
- 保留标准 CMSIS-DAP v2 GUID、4 MHz SWD 上限、Arm SWD 传输修复、响应隐式确认和 12–19 ms SWD 请求丢包重试。

## 1.0.19 - 2026-08-29

- 将 SWD 请求帧的到达 ACK 等待时间从 600–640 ms 缩短为 12–19 ms；无线请求丢失时快速重试，避免 Keil 连续 Flash Algorithm 流量因单次丢包长时间停顿。
- 普通配置和控制帧继续使用 120–160 ms ACK 等待时间；SWD 执行响应超时、200 µs ACK 转向保护和无线协议 v3 保持不变。

## 1.0.18 - 2026-08-29

- 将下一条 SWD 请求作为上一条 SWD 响应的隐式确认；显式响应 ACK 丢失时不再阻塞从机可靠槽 120–160 ms。
- 保留 SWD 请求到达 ACK、200 µs 转向保护、固定 profile 的 8 字节紧凑 ACK 和无线协议 v3。

## 1.0.17 - 2026-08-29

- 无线协议迭代为 v3；不保留旧无线固件兼容路径，主从机必须同时升级。
- 固定 profile 下使用 8 字节累计 ACK，并跳过不参与速率决策的 packet status SPI 读取。
- SWD 请求继续使用到达 ACK，完整 SWD 响应作为事务完成确认并支持重复请求缓存。
- 保留已验证的 200 µs ACK 转向保护；AUTO profile 继续使用完整 ACK 指标和自适应链路信息。

## 1.0.16 - 2026-08-29

- 严格对照 Arm CMSIS-DAP `SW_DP.c` 修正目标 SWD 传输收尾：仅在 `ACK=OK` 时执行配置的 idle cycles，并始终主动将 SWDIO 停车为高电平。
- 对 `WAIT/FAULT` 和协议错误分支补齐 Arm 要求的 turnaround/数据阶段回退时钟，避免后续写入或写后校验从错误线状态开始。
- 新增目标 SWD 协议对照回归测试；保持 4 MHz 目标 SWD 上限、无线协议 v3 和 Vendor Status v5。

## 1.0.15 - 2026-08-29

- 修复无线 `SWD_BLOCK` 对 CMSIS-DAP `DAP_Transfer` Match Value 读请求的编码：主机现在会发送 4 字节期望值，从机解码后按 Match Mask 比较。
- 对照 Arm CMSIS-DAP 2.1.2 官方 `DAP_SWD_Transfer()` 纠正 Value Mismatch 完成数：重试耗尽的 Match Value 项不计入 `Transfer Count`。
- 保留 4 MHz 目标 SWD 上限、Vendor Status v5 和无线协议 v3；本版本不恢复 Keil 定位期间的临时累计诊断字段。

## 1.0.14 - 2026-08-29

- 修复 CMSIS-DAP `DAP_Transfer` 的 Match Value 语义：保留完整 16 位重试次数，正确处理 AP posted read，并在不匹配时按配置重试。
- 清除 Keil 擦除失败定位期间加入的 USB/CMSIS-DAP 临时诊断计数和响应追踪代码，恢复 Vendor Status v5 接口。
- 保留标准 CMSIS-DAP v2 设备接口 GUID、FLRC ACK 后同步跳频和无线 SWD 批处理优化。

## 1.0.13 - 2026-08-29

- 增加 CMSIS-DAP Vendor `0x81` 响应追踪页，保存最近 9 条非诊断响应摘要和最后一条错误响应。
- 分别统计 CMSIS-DAP 核心生成响应、USB 响应入队、Bulk IN 启动和 Bulk IN 完成次数，用于定位 Keil RDDI-DAP 擦除失败的具体响应边界。

## 1.0.12 - 2026-08-29

- 扩展 CMSIS-DAP Vendor Status，记录最后一个 USB OUT 命令、最后一个成功提交命令、非查询 USB OUT 数、成功提交数和 USB 类初始化/反初始化次数。
- 诊断计数跨 USB 类重新初始化保留，并排除 Vendor Status 查询自身，用于区分 Keil 请求未到达 Bulk OUT、进入请求环后未提交和 USB 类重配置。

## 1.0.11 - 2026-08-29

- 保留 CMSIS-DAP 命令诊断跨 USB 类重新初始化的状态，使 Keil 失败收尾后的 USB 重配置不会清除根因证据。

## 1.0.10 - 2026-08-29

- 扩展 CMSIS-DAP Vendor Status，记录最后一个非 Vendor 命令、最后未知命令及累计次数，用于定位 Keil RDDI-DAP 擦除失败时的实际命令边界。
- 保持 Capabilities 仅声明 SWD 和独立 USB CDC，不声明尚未实现的 Atomic Commands。

## 1.0.9 - 2026-08-29

- 空闲链路只回到并停留在 rendezvous 频道，禁止主从机按各自时基自由扫描造成下一次 SWD 事务超时。
- 保持目标 SWD 的 4 MHz 实测上限；超过 4 MHz 的 `DAP_SWJ_Clock` 请求继续在最终执行边界钳制为 4 MHz。

## 1.0.8 - 2026-08-29

- 将 CMSIS-DAP v2 WinUSB 接口改为 Arm 标准设备接口 GUID，使 Keil 可以枚举无线 DAP。
- 修复 ACK 携带跳频目标时发送方未同步切换频道造成的周期性长尾；周期跳频只由 DATA 确认推进，健康 SWD 会话不再每 32 个 ACK 换频。
- 记录 USB、CMSIS-DAP、FLRC、无线从机和目标 SWD 的完整请求/响应链路及优化前时延基线。

## 1.0.7 - 2026-08-29

- 修复 SX1281 FLRC 包参数错误：`SetPacketParams` 现在使用 `0x08` 关闭 DC-free/whitening，与 Semtech 驱动一致。
- 增加从 GFSK 初始化切换到 `FLRC1M3` 和 `FLRC650K` 的驱动回归测试。

## 1.0.6 - 2026-08-28

- SWD 事务执行期间暂停主动扫频和 rendezvous profile 恢复，避免无线从机在主机等待响应时离开当前频道或速率。
- 无线 IRQ 调度移到 SWD 执行之前，并在 ACK `TX_DONE` 后先恢复 RX，避免 SWD 响应早于对端重新开启接收而触发长时间重试。
- 无线从机的 SWD 单轮批处理预算从 400 µs 提高到 1600 µs；有线模式仍使用 400 µs。

## 1.0.5 - 2026-08-28

- 修复无线 ACK 发送期间拒绝后续 `SWD_BLOCK` 请求的问题；后续 CMSIS-DAP 命令可先进入可靠队列，并在 ACK `TX_DONE` 后发送。

## 1.0.4 - 2026-08-28

- 修复 SX1281 从接收切换到发送前未进入 standby 的问题，确保驱动仅在合法状态下更新发送包参数。
- 包参数缓存仅记录最后一次成功写入的当前硬件状态；TX 实际帧长与 RX 最大载荷长度交替时，驱动会重新发送 `SET_PACKET_PARAMS`。
- 修复无线主机在发送上一条 SWD 响应的 ACK 时拒绝下一条 CMSIS-DAP 命令的问题；下一条命令可以先进入可靠队列，并在 ACK `TX_DONE` 后发送。

## 1.0.2 - 2026-08-28

- 整理 `STAT_LED` 状态指示：初始化失败时红灯每 200 ms 翻转，运行错误时每 500 ms 翻转；正常运行按无线主机、无线从机和有线模式分别显示蓝色、绿色和青色，空闲、通信和 SWD 烧录时分别按 1000 ms、450 ms 和 150 ms 翻转。
- 无线从机 USB 改为仅暴露 MSC 配置磁盘，不再暴露 CDC 和 CMSIS-DAP 接口；切换设备模式后保存配置并复位，再按新模式重新枚举 USB。
- 修复 Bootloader 回退时红蓝交替指示、无线启动时短暂显示有线青色、DFU 配置文件编码和无线 ACK 转向等待时间问题。

## 1.0.1 - 2026-08-27

- 修复 SX1281 包参数缓存错误，确保发射后切回接收时重新写入当前接收载荷上限。

## 1.0.0 - 2026-08-27

- 收紧无线 SWD Sequence 的 bit_count 边界，拒绝会因 `uint8_t` 截断而形成的畸形请求，并增加最大载荷回归测试。
- 修复 Windows PowerShell 下 GCC 子工具、主机测试编译器和 Git 全局 excludes 权限警告导致的门禁不稳定问题。
- 将项目文档合并为项目、硬件、无线、开发与发布四本手册，保留 EasyEDA U5 原理图连接记录作为独立事实来源。
- 将固件版本更新为 `1.0.0`。
- 为 Bootloader DFU `28E9:1291` 增加 Microsoft OS 1.0 WCID 描述符，使 Windows 8 及以上系统自动绑定 WinUSB。
- 让 Bootloader DFU 接口报告非活动槽、加载地址、已确认版本和恢复模式，并按更新后的原理图及实板单色测试统一 `PC13/PC14/PC15` 的蓝/红/绿映射。

## 0.8.0-rc.3 - 2026-06-11

- 将空中协议定为 v1，增加可靠的无线 `SWD_ABORT` 控制帧。
- 从机先确认无线 SWD 请求再执行，并在目标 WAIT 重试期间轮询取消请求。
- 将单次 SWD WAIT 限制为 250 ms，整个隧道请求限制为 2500 ms。
- 拒绝非法的 Match Mask/Match Value 读写组合。
- 修复首次射频初始化失败后恢复时缺少新会话广播的问题。
- 为 USB 配置盘重建增加稳定断开窗口，移除失败路径中的重复重连。
- 修复 Linux CI 的厂商快照指纹和独立头文件编译问题。

## 0.8.0-rc.2 - 2026-06-09

- 修正 ResetTarget、Disconnect 和 Match Mismatch 的 CMSIS-DAP 语义。
- 限制异常 WAIT/Match Retry 时长，并封堵无线 SWD Sequence 越界入口。
- 修复畸形无线 SWD 分块响应可能导致的事务数组越界。
- 修复 GFSK 同步字有效 IRQ 可能提前重启接收的问题。
- 增加 USB 描述符、WCID、PMA 和协议截断输入测试。
- 增加配置 Flash 双副本掉电故障模拟与源码树发布指纹。
- 增加第三方许可归属、硬件验收流程和 pyOCD 冒烟脚本。
- 修复 CDC `SET_LINE_CODING` 非法长度导致的控制缓冲区覆盖风险。
- 正确声明 SWD 与独立 USB CDC COM Port 能力，不宣称支持 DAP UART 命令。
- 增加连续 Release 构建的字节级可重复性发布门禁。
- 将 CMSIS-DAP 固定为 Git submodule；GD32 V3.0.3 厂商快照由哈希锁保护。
- 移除对修改版 GD32 USB 库的隐式依赖，CDC、WCID 与 MSC 适配移入项目层。
- 拦截越界 USB 标准请求、非法 recipient/端点，并修复 CDC 单包接收延迟及控制请求校验。
- 将 GCC 工具链适配移至 `firmware/toolchain`，并强制 GCC/Keil 构建产物与 IDE 临时文件不得污染源码树。
- 统一正式项目名称为 `DAPLink-Wireless`，保留 `daplink_wireless` 作为构建产物 basename。
- 增加 GitHub Actions 软件门禁，修复 Windows/Linux 构建脚本可移植性，并将完整 GPLv3 许可证纳入发布包校验。
- 将 9 个重复的主机测试脚本合并为数据化 `test_host.ps1`，并使用 SPDX 精简脚本许可证头。

## 0.8.0-rc.1 - 2026-06-09

- 实现 CMSIS-DAP v2 Bulk、SWD 命令映射、取消与超时处理。
- 实现有线、无线主机和无线从机三种设备模式。
- 实现 SX1281 GFSK/FLRC、RSSI 回传、链路自适应和确定性跳频。
- 实现 CDC 串口参数同步与双向可靠透传。
- 实现 MSC 配置磁盘、Flash 原子配置存储和按键配置。
- 增加看门狗、复位原因、无线诊断计数和状态文件。
- 增加主机侧协议测试、严格 GCC/Keil 构建及发布产物清单。

这些版本均为工程发布候选，不是公开量产版本。
