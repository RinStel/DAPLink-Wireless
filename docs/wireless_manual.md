# 无线手册：协议 v2、跳频与射频验证

## 当前版本与兼容边界

当前固件只使用无线链路协议 `v2`。无线帧头偏移 2 的协议版本字段为 `02`，代码
中的唯一版本常量是 `RADIO_PROTOCOL_VERSION 2U`。接收端拒绝所有其他版本；项目
不保留 v1 解析、发送、协商或回退路径。

## 帧格式

所有多字节整数使用大端序。

| 偏移 | 长度 | 内容 |
| --- | --- | --- |
| 0 | 2 | 固定魔数 `44 53` |
| 2 | 1 | 协议版本 `02` |
| 3 | 1 | 帧类型 |
| 4 | 4 | 同步码派生的网络 ID |
| 8 | 4 | 本次启动会话 ID |
| 12 | 4 | 递增序号 |
| 16 | 1 | 负载长度 |
| 17 | 0..110 | 负载 |

接收端使用会话 ID、序号、类型、长度和负载摘要共同识别重复帧。每次射频初始
化都会可靠发送 `SESSION_START`；接收端无条件接受 `SESSION_START` 帧并清空上一会话的重复
包缓存，因此协议正确性不依赖随机数绝对唯一。

无线主机取消正在等待的 CMSIS-DAP Transfer 时发送可靠的 `SWD_ABORT` 控制帧，
负载为 1 字节 transaction ID。无线从机在本地 WAIT 重试期间轮询射频 IRQ，
收到匹配事务后中止执行并丢弃结果。单次底层 WAIT 最多占用 250 ms，整个隧道
请求最多占用 2500 ms。主机收到 `SWD_ABORT` 的可靠 ACK 后才向 USB 返回原
Transfer 的终止响应。

## DATA 窗口与 ACK

`DATA` 每个方向最多保留 4 个未确认槽位。接收端可以乱序缓存，ACK 使用
`ack_next + bitmap` 累计确认，重复帧不会再次投递到 UART 或 CDC。控制帧仍使用
单独的可靠槽位。

ACK 负载固定为 17 字节：

| 偏移 | 内容 |
| --- | --- |
| 0..3 | 下一个按序序号 `ack_next` |
| 4..7 | 位图确认，bit0 对应 `ack_next` |
| 8 | 标志；bit0 为 `HOP_VALID` |
| 9 | 下一频道索引 |
| 10..11 | 接收 RSSI，单位 0.5 dBm，有符号大端整数 |
| 12 | SX1281 packet error status |
| 13 | SX1281 TX/RX status |
| 14 | SX1281 sync address status |
| 15 | 接收端当前 profile |
| 16 | 接收端当前射频频道索引 |

## 压缩 SWD_BLOCK

无线 SWD Transfer 使用 `SWD_BLOCK` 和 `SWD_BLOCK_RESPONSE`。请求依次包含
`transaction_id`、transfer 数量、每个请求字节和仅写传输的数据。一个块最多包含
21 个普通 transfer，响应只携带已完成读传输的数据，并保持读顺序。无线主机将
响应映射回 CMSIS-DAP 的原始传输顺序。

发送端仅在 `PROFILE=AUTO` 时将 ACK RSSI 输入 EWMA 和速率决策。固定
`PROFILE` 模式只记录指标，不改变 profile。

## Profile 切换与自适应

无线主机拥有切速控制权：

1. 主机使用旧 profile 可靠发送 `PROFILE_SWITCH`。
2. 从机使用旧 profile 回复带指标的 ACK。
3. 从机在 ACK 发完后切换到目标 profile，并启动 300 ms 试用窗口。
4. 主机收到 ACK 后切换，并立即可靠发送 `PROFILE_CONFIRM`。
5. 从机收到确认或目标 profile 下的有效业务帧后结束试用。
6. 确认未到达时，从机在试用窗口超时后回退旧 profile。
7. 切换 ACK 丢失时，主机继续在旧 profile 重传；从机回退后重新接收控制帧。
8. 目标 profile 下的确认重试耗尽时，主机也回退到切换前 profile。

支持的 profile：

- GFSK 2 Mbps
- GFSK 1 Mbps
- GFSK 500 kbps
- FLRC 1.3 Mbps，CR 3/4
- FLRC 650 kbps，CR 3/4

当前自适应阈值：

| 接收 RSSI | 推荐 profile |
| --- | --- |
| `>= -55 dBm` | GFSK 2 Mbps |
| `>= -68 dBm` | GFSK 1 Mbps |
| `>= -80 dBm` | FLRC 1.3 Mbps，CR 3/4 |
| `>= -90 dBm` | GFSK 500 kbps |
| `< -90 dBm` | FLRC 650 kbps，CR 3/4 |

检测到 CRC、同步、长度或中止错误时直接降至 FLRC 650 kbps。无线恢复会重新
使用配置中的初始 profile；`PROFILE=AUTO` 的初始值为 GFSK 1 Mbps。
`PROFILE=AUTO` 下任一端连续 500 ms 没有收到有效帧时，也回到 GFSK 1 Mbps
会合 profile。

## 确定性跳频

无线链路采用包级自适应跳频，不依赖 Bluetooth 的 1600 hop/s 时钟槽。`DATA`
使用四槽窗口；控制帧和 ACK 仍按帧顺序处理。

### 频道表

- 2405 MHz 至 2480 MHz 共 16 个频道。
- 频道间隔为 5 MHz，为最宽 2.4 MHz GFSK 带宽保留余量。
- 同步码派生网络 ID、频道排列和初始会合频道。
- 全部频道位于 SX1281 的 2400..2483.5 MHz 工作范围内。

### 正常切换

无线主机每累计 32 个成功业务确认请求下一频道。下一频道和 `HOP_VALID`
标志直接合并到累计 ACK；对端收到有效 ACK 后切换频道，不再发送独立的
`HOP_SWITCH`/`HOP_CONFIRM` 数据帧。

只有主机发起计划切换，避免双方同时提出冲突目标。

### 受阻频道恢复

ACK 除 RSSI 和 SX1281 状态外还报告接收频道。来自其他频道的 ACK 不接受为当前
待处理事务的确认。

每次业务帧失败都会惩罚失败业务帧使用的频道。连续 180 ms 没有有效帧后，双方进入相同的
500 ms 恢复周期：前 300 ms 停留在同步码派生的会合频道，随后按相同的确定性
频道序列以 35 ms 驻留时间扫描。重传也使用同一恢复函数，发送端和接收端不会
独立遍历不一致的候选频道。

成功交换会降低本地频道惩罚；每 32 次成功事务衰减一次全部惩罚，使暂时受阻
的频道最终可以重新探测。当前实现只在本地保存频道质量，不交换 Bluetooth
式共享 AFH 频道表。

## 性能审查与实施状态（2026-08-26）

当前软件已具备 v2 的 110 字节应用负载、四槽 DATA 窗口、累计 ACK、压缩
`SWD_BLOCK`、GFSK/FLRC profile 和 64 字节 CDC/DAP USB 端点。上一轮已实施以下
软件优化，本轮完整门禁已复核通过：

1. CDC 传输层增加有界软件队列，将 110 字节业务负载拆成 64 字节 USB 包；源端
   只有在目的端完整入队后才消费数据。
2. 无线 DATA 窗口满时在读取 CDC/UART 前施加背压。
3. 将 CMSIS-DAP 内部 SWD chunk 从 10 提高到 16，同时保留包长度校验。
4. 在异步 SWD 执行器中按次数和 2500 ms 执行预算重试 WAIT，并保留取消路径。
5. 在 `target_swd.c` 直接读取 `DWT->CYCCNT`，并为 SX1281 增加四项有限
   `SET_PACKET_PARAMS` 缓存。

此外，`main()` 已调整为先调用 `usb_config_disk_init()`、再调用
`serial_bridge_init()`。这会避免无线 SX1281 同步初始化阻塞 USB D+ 上拉，针对连接
电脑后长时间识别不到 DAP 的现象提供软件修复。USB 驱动绑定和硬件链路仍需实测。

待硬件验收的项目包括：SWCLK 波形和烧录时间、无线半双工 ACK 调度与 goodput、
3 Mbps UART/CDC PRBS 数据完整性，以及 Windows/Linux/macOS 的枚举和恢复。

详细的 RED/GREEN 步骤见
[`2026-08-26-throughput-followup-implementation.md`](superpowers/plans/2026-08-26-throughput-followup-implementation.md)。
在双板、逻辑分析仪、目标 Cortex-M 和 3 Mbps PRBS 条件具备前，不把这些项目标记
为硬件完成。

## 当前射频参数

| 参数 | 值 |
| --- | --- |
| 频率 | 2450 MHz |
| 模式 | GFSK |
| 速率/带宽 | 1.0 Mbps / 1.2 MHz |
| 调制指数 | 0.5 |
| 高斯滤波 | BT 0.5 |
| 同步字 | `0xD391DA26A5`，40 位 |
| CRC | CRC-16/CCITT，`poly=0x1021`，`init=0xFFFF` |
| 白化 | 开启 |
| SX1281 输出 | -2 dBm；对应 E28 外部 PA 约 20 dBm |

## CMSIS-DAP 诊断状态

`DAP Vendor 0x80` 状态格式版本为 5，原有字节 0..20 保持不变：

| 偏移 | 内容 |
| --- | --- |
| 21..22 | 对端 RSSI，单位 0.5 dBm，有符号小端整数 |
| 23 | 当前 profile |
| 24 | 本次启动的 profile 切换次数 |
| 25 | 对端 packet error status |
| 26 | 对端 TX/RX status |
| 27 | 对端 sync address status |
| 28 | 当前射频频道索引 |
| 29 | 本次启动的频道切换次数 |
| 30 | 上次复位原因位图 |
| 31..34 | 本次启动运行时间，单位 ms |
| 35..38 | RX/TX timeout IRQ 次数 |
| 39..42 | 协议校验失败帧数 |
| 43..46 | 对端 session 变更次数 |

状态标志字节 bit3 表示已经收到有效的对端链路指标。

## 双板射频冒烟测试

### 固件行为

- 绿灯约 1 Hz 闪烁：主循环和射频初始化正常。
- 红灯常亮：SX1281 复位、BUSY、SPI、配置回读或运行过程失败。
- 蓝灯短亮：本板开始发送诊断帧，或收到另一块板的有效诊断帧。

LED 通道与 MCU GPIO 的实际对应关系以[硬件手册](hardware_manual.md)为准。
2026-08-27 实板低电平单色测试确认 `PC13=蓝`、`PC14=红`、`PC15=绿`。更新后的
EasyEDA 网络名分别为 `STAT_LED_B`、`STAT_LED_R`、`STAT_LED_G`，与实板发光颜色一致。

### 测试步骤

1. 分别给两块板烧录 `build/gcc/release/daplink_wireless.hex`。
2. 首次上电使用限流电源，确认两块板均红灯灭、绿灯闪烁。
3. 两块板相距至少 1 米，确认 `M20SX`/E28 外接天线已正确安装。
4. 按下 A 板按键；A 板蓝灯应短亮，B 板蓝灯也应短亮。
5. 按下 B 板按键，确认反向链路结果相同。
6. 重复至少 100 次，确认无红灯、失联或异常复位。

### 红灯常亮时

依次检查：

1. `NRESET` 是否出现低脉冲并恢复高电平。
2. 复位后 `BUSY` 是否最终拉低。
3. SPI0 的 PA5/PA6/PA7 是否有波形，NSS 是否在 PA4。
4. SPI 模式是否为 Mode 0，时钟约 7.5 MHz。
5. MISO 是否返回非固定高电平或低电平。
6. 模块 3.3 V 电源在发射瞬间是否稳定。

射频控制脚的最新 MCU 映射和代码状态见[硬件手册](hardware_manual.md)。在
`RF_RX_EN`、`RF_TX_EN`、`RF_NRESET`、`RF_BUSY` 和 `RF_DIO1` 的真实电平、
时序与模块响应尚未完成板级验收前，不得把冒烟失败归因于无线参数本身。
