# Changelog

## Unreleased

- **修复 MSC 将 `USBD_EP0_MAX_SIZE` 与 `MSC_DATA_PACKET_SIZE` 互换导致的 PMA
  溢出**（由未提交变更引入，复现“插入后近一分钟 DAP 不可访问”）。厂商
  `usbd_ep_data_write()` 既不按 maxpacket 也不按端点 PMA 槽长夹取，且
  `tx_count` 取请求字节数；而 MSC 槽只有 32 B 时，`scsi_process_read()`/
  `scsi_write10()` 会按 `MSC_MEDIA_PACKET_SIZE`（512 B）一次交整个扇区，从
  0x00C8 连写 512 B 踩过 CDC 通知/数据、`s_packet_in`（DAP 回复）并越出
  0x200 的 PMA 末尾，OUT 方向同样越界至 0x2A8。与实测完全吻合：CDC 仍可打开
  （SET_LINE_CODING 走 EP0）、仅 MI_00(MSC) 与 MI_03(WinUSB) 两个子设备进
  `LIBUSB_ERROR_NOT_SUPPORTED`、全程不掉线不跳问题码。
  现在 SCSI 数据平面改由 `firmware/usb/usb_msc_scsi.c` 实现（停止编译厂商同名
  文件，保留未改动的 `usbd_msc_bbb.c`）：扇区暂存在 512 B RAM 缓冲
  `bbb_data`，每次只交不超 `MSC_DATA_PACKET_SIZE` 的片，接收侧只信
  `xfer_count`；同时恢复 HEAD 的 PMA 几何（EP0 32 B / MSC 64 B）。厂商 SCSI 层
  传给 `mem_read`/`mem_write` 的第 3 参是“块数”，而 `usb_config_disk.c` 的
  `disk_read`/`disk_write` 按“字节地址”语义实现（内部再乘块大小），新实现按
  字节地址传参与后者一致。`usbd_conf.h` 新增 3 条 `_Static_assert`：扇区必须是
  端点事务长度的整数倍、MSC 槽必须容得下 31 字节 CBW 与 13 字节 CSW；新测
  `usb-msc-scsi` 在桩函数里逐事务核长度与拼接内容。实机复测：插入后 **5.06 s**
  DAP 已可用（修复前同方法测得故障窗口 60.6 s，窗口内 1208 次 `attach` 均失败于
  `LIBUSB_ERROR_NOT_SUPPORTED`，且 CDC 可打开）。

## 1.0.53 - 2026-08-30

- 新增 CDC 回显吞吐基准（RADIO_FRAME_LOOPBACK 帧 + vendor 0x82 开关 +
  	ools/radio_throughput_test.py）：从机把收到的 DATA 原路回显，
  测得的是当前 DATA 窗口架构下的射频往返天花板。

## 1.0.52 - 2026-08-30

- 诊断构建随 1.0.51/1.0.52 代码同步重新发布，用于测量 64 KiB 烧录时
  的逐命令计时分布；无功能变化。

## 1.0.51 - 2026-08-30

- 速度与清理：流水线深度全线 4 到 8（CMSIS-DAP 对外窗口、命令 FIFO、
  USB 请求/响应环、桥接期望事务 FIFO、射频请求队列），摊薄每命令固定往返
  开销；删除 serial_bridge_scheduler.h 伪配置层（全部为常量返回值的间接
  层）、swd_queued/single_swd 诊断桩、以及内部 API 的防御性 NULL 检查
  （USB/射频帧解析的信任边界校验保留）。
- 响应 ACK 在主机仍有排队请求时跳过（下一请求构成隐式确认），省去每次
  事务的 ACK 转向。

## 1.0.50 - 2026-08-30

- 修复 1.0.49 流水线在烧录负载下的两个缺陷：CMSIS-DAP 各槽位改用独立
  transfer 表（共享暂存区会被后派发命令覆盖，导致读数据映射错乱并误报
  Transfer 错误）；SWD 请求重传改为指数退避（12/24/48/96/192/384/768 ms）
  并移除 5 次重传即杀射频的逻辑——擦除与 flash 算法轮询期间从机长时间无
  响应属于合法执行。

## 1.0.49 - 2026-08-30

- 无线 SWD 改为交替流水线：主机侧允许最多 4 个 `DAP_Transfer`/`DAP_TransferBlock`
  在途，射频层同一时刻只发送一个请求，收到端到端响应后立即推进下一请求，
  消除每个 block 的固定请求 ACK 往返（基线单事务约 3.7 ms）。主从机必须
  同时升级到本版本。
- CMSIS-DAP 命令核心重写为 4 槽流水线：Transfer 类命令流水推进，控制命令
  作为屏障，响应严格按命令顺序交付；`DAP_ExecuteCommands` 子命令按序拼装。
- 删除从未成功发送且容量必然不足的 SWD_BURST 全链路（主机聚合、隧道编解码、
  从机执行与响应），以及从未接入主路径的 `wireless_swd_pipeline` 半成品模块；
  移除废弃的 `HOP_SWITCH`/`HOP_CONFIRM` 帧类型与对应诊断页。
- 无线协议版本号与文档对齐为 `RADIO_PROTOCOL_VERSION 4U`（1.0.36 起的线格式
  语义变化此前未同步版本常量）。旧固件将直接拒绝新版本帧，可避免静默不兼容。
- 移除以源码字符串匹配实现的伪测试（reliability/turnaround/hot-path），
  协议测试改写为流水线语义；`tools/dap_diagnostics.py` 随诊断页 3 移除同步精简。

## 1.0.40 - 2026-08-30

- 保持无线 ACK 转向保护窗口为 50 µs，并通过 README 规定的 MSC/DFU 流程完成主从双板升级。
- 64 KiB pyOCD 实测完成样本为 5.78–6.16 kB/s，较 5.30 kB/s 基线约提升 13%；后续一次测试出现探针掉线，双板稳定性仍待确认。

## 1.0.38 - 2026-08-30

- 诊断构建增加 Burst 解析、110 字节容量和桥接状态拒绝计数，用于定位 1.0.37 实测仍未发送 Burst 的原因。

- 将 ACK 发送前的射频转向保护窗口从 200 µs 降至 50 µs；窗口保留为 `SERIAL_BRIDGE_ACK_TURNAROUND_DELAY_US` 可配置宏。必须通过实机稳定性和吞吐测量确认收益。

## 1.0.37 - 2026-08-30

- 修复 CMSIS-DAP v2 Bulk OUT 的 64 字节填充包被 Burst 白名单拒绝的问题。Burst 解析现在使用命令的实际编码长度，并忽略 USB 包尾部填充；`1.0.36` 实测因该问题未发送任何 Burst。

## 1.0.36 - 2026-08-30

- 无线协议递增到 v4；主从机必须同时升级，不保留 v3/v2/v1 兼容路径。
- 无线主机仅聚合 USB 请求环中已经到达的 2 至 3 个 `DAP_Transfer` 或 `DAP_TransferBlock`，不等待后续请求。
- Burst 帧保留每个 SWD 子块的边界。无线从机逐块执行，分别完成 AP posted-read、`DP_RDBUFF`、写后检查和错误计数，再返回一个带子响应边界的 Burst 响应。
- Burst 请求、最坏响应和所有长度前缀必须在 110 字节负载内；容量不足、边界命令或解析失败时保留单发路径和对应错误响应。
- 诊断构建增加 Burst 次数、平均子块数、单发次数、负载字节、回退和解析错误统计；普通 Release 继续通过条件编译移除诊断热路径。
- `DAP_Info(0xF0)` 继续只声明 SWD 能力，不默认声明 Atomic Commands；Keil 兼容性结论不变。

## 1.0.35 - 2026-08-29

- 在诊断构建中记录 CMSIS-DAP USB 请求环的最大、累计和采样深度，用实测结果判定无线 burst 是否有可聚合的主机请求。

## 1.0.34 - 2026-08-29

- 递增诊断镜像版本，使 A/B 更新器可以将启用 `CMSIS_DAP_DIAGNOSTICS` 的 Release 镜像与普通 `1.0.33` 区分。

## 1.0.33 - 2026-08-29

- 增加可选的 `CMSIS_DAP_DIAGNOSTICS` 构建开关，记录 USB、无线 ACK、Remote 往返、USB IN、帧字节、重传和 Transfer 大小分布。
- `DAP_VENDOR_TRACE (0x81)` 在诊断构建中支持统计清零和三页固定数据读取；普通构建完整裁剪热路径埋点和统计存储。
- 增加 `tools/dap_diagnostics.py reset|dump`，用于导出 JSON 指标；本版本不修改无线协议、profile、窗口或 SWD 时钟。

## 1.0.32 - 2026-08-29

- 临时关闭 `DAP_Info(0xF0)` 的 Atomic Commands 能力广告，迫使 Keil 使用普通 CMSIS-DAP 传输路径；批量命令实现保留。
- 保留 Debug 构建的 CMSIS-DAP 请求/响应追踪接口（`0x81`）；Release 构建通过编译宏裁剪追踪存储。
- 实机验收：无线主机/从机使用本版本通过 F10x 工程的 Keil `Erase`、`Programming` 和 `Verify`。

## 1.0.28 - 2026-08-29

## 1.0.29 - 2026-08-29

- 对齐官方 CMSIS-DAP：`DAP_Transfer` 和 `DAP_TransferBlock` 的 transfer count 为 0 时返回空成功响应，支持 Keil 编程收尾阶段的空传输。

- 回退强制 64 字节 USB IN 响应；恢复官方 CMSIS-DAP 短响应包长度，修复 Keil 设备探测受影响的问题。

## 1.0.27 - 2026-08-29

- USB IN 响应统一按 64 字节 CMSIS-DAP v2 包发送，短响应尾部补零，避免 Keil 编程进度完成后因最后一个短包判定失败。

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
