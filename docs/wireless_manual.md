# 无线手册：协议 v3、跳频与射频验证

## 当前版本与兼容边界

当前固件只使用无线链路协议 `v3`。无线帧头偏移 2 的协议版本字段为 `03`，代码
中的唯一版本常量是 `RADIO_PROTOCOL_VERSION 3U`。接收端拒绝所有其他版本；项目
不保留 v2/v1 解析、发送、协商或回退路径。主从机必须同时升级。

## 帧格式

所有多字节整数使用大端序。

| 偏移 | 长度 | 内容 |
| --- | --- | --- |
| 0 | 2 | 固定魔数 `44 53` |
| 2 | 1 | 协议版本 `03` |
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

ACK 有两种 v3 布局：AUTO profile 或携带跳频信息时使用完整 17 字节；固定
profile 且没有待处理跳频时使用 8 字节紧凑布局。

完整 ACK 负载为 17 字节：

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

紧凑 ACK 负载为：

```text
ack_next[4] | bitmap[4]
```

紧凑 ACK 不更新 RSSI、错误状态或 profile 诊断值；主机保留上一次完整 ACK
的指标。固定 profile 下 ACK 的确认和重传语义不变。

AUTO profile 只接受完整 ACK。接收路径在固定 profile 下跳过 `GET_PACKET_STATUS`
SPI 事务；AUTO profile 仍读取 RSSI、错误状态和同步状态，用于链路自适应。

## 压缩 SWD_BLOCK

无线 SWD Transfer 使用 `SWD_BLOCK` 和 `SWD_BLOCK_RESPONSE`。请求依次包含
`transaction_id`、transfer 数量、每个请求字节和仅写传输的数据。一个块最多包含
21 个普通 transfer，响应只携带已完成读传输的数据，并保持读顺序。无线主机将
响应映射回 CMSIS-DAP 的原始传输顺序。

发送端仅在 `PROFILE=AUTO` 时将 ACK RSSI 输入 EWMA 和速率决策。固定
`PROFILE` 模式只记录指标，不改变 profile。

## USB 到目标 SWD 的完整链路

以下链路适用于无线主机。无线从机的 USB 只暴露 MSC 配置盘，不直接接收
CMSIS-DAP 命令。

### 请求路径

1. 主机软件通过 CMSIS-DAP v2 的 bulk OUT 端点 `EP5 OUT (0x05)` 发送一个
   64 字节以内的 CMSIS-DAP 包。
2. `cmsis_dap_usb.c` 将包写入请求环。请求环包含 6 个物理槽并保留一个空槽；
   对外公布的流水线深度为 4。`DAP_TransferAbort` 在 USB OUT 回调中直接设置
   取消标志，不等待普通请求出队。
3. `cmsis_dap_usb_process()` 只在 CMSIS-DAP 命令核心空闲时提交下一请求。
   USB 可以提前排队，但 `cmsis_dap.c` 同时只推进一个活动命令，以保持响应顺序。
4. `DAP_Info` 和 `DAP Vendor 0x80` 在无线主机本地生成响应。连接、时钟、引脚、
   SWJ/SWD Sequence 和 Transfer 命令进入 `serial_bridge`。
5. 普通控制命令编码为可靠的 `SWD_COMMAND`；Transfer 编码为压缩的
   `SWD_BLOCK`。请求区先排列 Transfer Request，再按顺序排列写请求和
   Match Value 读请求携带的 4 字节数据。每个无线帧添加 17 字节 v2
   v3 帧头、会话 ID 和序号。
6. `serial_bridge` 同时只保留一个可靠控制事务。SX1281 在当前固定 profile 和
   频道发送该帧；FLRC1M3 使用 1.3 Mbps、CR 3/4。
7. 无线从机读取 RX FIFO、验证 CRC、同步字、网络 ID、会话、长度和重复帧键，
   再把 SWD 请求交给唯一的 `swd_bridge_service` 所有者。
8. 从机用旧频道和旧 profile 发送请求 ACK。该 ACK 只确认无线请求已到达，
   不表示目标 SWD 已执行完成；固定 profile 下它使用 8 字节紧凑布局。
9. ACK 的 `TX_DONE` 完成后，从机恢复 RX，再由 `swd_tunnel` 执行目标 SWD。
   无线从机每轮主循环最多使用 1600 µs 批处理预算；长 block 分轮执行，并保留
   WAIT、2500 ms 总预算和 Abort 检查。

### 响应路径

1. 从机把目标 ACK、完成数量和读数据编码为 `SWD_BLOCK_RESPONSE`，并按可靠帧
   发送。该完整响应确认目标事务完成。重复的相同请求不会再次执行目标 SWD；
   从机重发缓存响应。
2. 无线主机收到响应后检查会话和 transaction ID，将结果交回
   `swd_bridge_service`，并发送响应 ACK。
   CMSIS-DAP 核心同时只推进一个命令，因此从机收到下一条 SWD 请求时，也可将其
   作为上一响应已到达主机的隐式确认；显式响应 ACK 丢失不会再占用旧响应槽直至
   120 ms 级重试超时。
3. `cmsis_dap_process()` 将无线结果恢复为原 CMSIS-DAP 响应。多于一个内部
   chunk 的 Transfer 继续提交下一 chunk；全部完成后才结束该 USB 命令。
4. `cmsis_dap_usb.c` 将响应写入响应环，再通过 `EP5 IN (0x85)` 按请求顺序发送。

```text
Keil/pyOCD   USB主机DAP       FLRC主机       FLRC从机       目标MCU
    | EP5 OUT    |                |              |              |
    |----------->| SWD_BLOCK      |              |              |
    |            |--------------->|----请求----->|              |
    |            |                |<----ACK------|              |
    |            |                |              |----SWD------>|
    |            |                |              |<---ACK/数据--|
    |            |                |<---响应-------|              |
    |            |                |----ACK------->|              |
    |<-----------| EP5 IN         |              |              |
```

### Arm CMSIS-DAP 对照基线

2026-08-29 使用 Arm 官方 CMSIS-DAP 仓库提交
`12636590eec66fae2d1bba4518749426ad5a4595` 对照命令语义。该提交的
`DAP_FW_VER` 为 2.1.2。工程内 `Third-Party/CMSIS-DAP/Firmware/Source/DAP.c`
与官方文件内容一致，只有换行符差异。

| 项目 | Arm 语义 | 当前实现 |
| --- | --- | --- |
| 能力位 | 按实现声明 SWD、JTAG、Atomic、SWO 和 UART | 只声明 SWD 和独立 USB CDC |
| Match Mask | 写请求携带 4 字节 mask，不访问目标寄存器 | 一致 |
| Match Value | 读请求携带 4 字节期望值 | USB 解析和无线 `SWD_BLOCK` 均保留该值 |
| Match Retry | 首次读取后最多重试配置的 16 位次数 | 一致 |
| Value Mismatch | 设置响应位 4；失败项不计入 `Transfer Count` | 一致 |
| AP 读 | 先发 posted read，再取回上一次读数据 | 一致；Match Value 重试也使用 posted read |
| Timestamp | 可选能力 | 不声明该能力，并拒绝带 bit 7 的 Transfer |
| SWD 时钟 | 由适配层决定 | 最终执行边界限制为 4 MHz |
| 超时 | Match Retry 本身没有独立时间超时 | 另有 2500 ms 隧道预算和 4000 ms CMSIS-DAP 命令超时 |

`DAP_TransferBlock` 不支持 Match Mask 或 Match Value。Keil 发送的命令是
`DAP_Transfer`；无线实现在 CMSIS-DAP 解析后才将多个 Transfer 压缩为
私有 `SWD_BLOCK`。

### 时延和超时

正常单个 SWD 命令包含请求、请求 ACK、响应和响应 ACK。固定 profile 下两个 ACK
都从 17 字节缩短为 8 字节。因此 FLRC
空口速率只决定其中一部分时延。USB 调度、RX/TX 转向、SX1281
SPI 命令、从机主循环和目标 SWD 执行也会进入端到端时延。

2026-08-29 在 `FLRC1M3`、1 MHz SWD 配置下连续执行 200 次 `DAP_SWJ_Clock`：

| 指标 | 优化前结果 |
| --- | ---: |
| 最小值 | 3.706 ms |
| 中位数 | 3.999 ms |
| P95 | 4.231 ms |
| 最大值 | 625.470 ms |
| 超过 20 ms | 9/200 |

约 625 ms 长尾按约 32 次 ACK 周期出现。根因不是 FLRC1M3 的调制速率，而是计划
跳频时 ACK 发送方没有在 `TX_DONE` 后同步切换，双方进入 600 ms 级 SWD ACK
超时/恢复。优化后必须使用相同命令和样本数复测。

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

无线链路采用包级确定性跳频，不依赖 Bluetooth 的 1600 hop/s 时钟槽。`DATA`
使用四槽窗口；控制帧和 ACK 仍按帧顺序处理。

### 频道表

- 2405 MHz 至 2480 MHz 共 16 个频道。
- 频道间隔为 5 MHz，为最宽 2.4 MHz GFSK 带宽保留余量。
- 同步码派生网络 ID、频道排列和初始会合频道。
- 全部频道位于 SX1281 的 2400..2483.5 MHz 工作范围内。

### 正常切换

无线主机每累计 32 个成功 DATA 确认请求下一频道。健康的 SWD 会话不推进周期
跳频计数；发生实际超时后仍可以进入受阻频道恢复。下一频道和 `HOP_VALID` 标志
直接合并到 ACK；对端收到有效 ACK 后切换频道，发送该 ACK 的一端在
`TX_DONE` 后切换到同一频道。不再发送独立的 `HOP_SWITCH`/`HOP_CONFIRM` 数据帧。

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

候选频道由 `network_id`、跳频代数和失败惩罚确定。实现不扫描全部频道，也不按
RSSI 或 packet error status 排序。因此该机制是“失败信道回避”，不是“选择更优
信道”。

## 性能审查与实施状态（2026-08-26）

当前软件已具备 v3 的 110 字节应用负载、四槽 DATA 窗口、累计 ACK、压缩
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
| 模式 | GFSK 或 FLRC，取决于当前 profile |
| 速率/带宽 | GFSK：1.0 Mbps / 1.2 MHz；FLRC：1.3 Mbps / 1.2 MHz 或 650 kbps / 0.6 MHz |
| 调制指数 | 0.5 |
| 高斯滤波 | BT 0.5 |
| 同步字 | GFSK：`0xD391DA26A5`，40 位；FLRC：配置同步字前 4 字节，32 位 |
| CRC | GFSK：CRC-16/CCITT，`poly=0x1021`，`init=0xFFFF`；FLRC：2 字节 CRC |
| 白化 | GFSK 开启；FLRC 关闭 |
| SX1281 输出 | -2 dBm；对应 E28 外部 PA 约 20 dBm |

## CMSIS-DAP 状态

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

- 无线主机正常运行时显示蓝色；无线从机正常运行时显示绿色。
- 空闲时对应颜色每 1000 ms 翻转一次。
- 有通信但没有 SWD 烧录时，对应颜色每 450 ms 翻转一次；正在执行 SWD
  烧录时每 150 ms 翻转一次。
- SX1281 复位、BUSY、SPI、配置回读或运行过程失败时，红灯每 500 ms
  翻转一次；启动初始化失败时，红灯每 200 ms 翻转一次。
- 有线模式使用绿蓝同时点亮表示青色，并采用相同的空闲、通信和烧录节奏。
- 无线从机仅向 USB 暴露 MSC 配置磁盘，不暴露 CDC 或 CMSIS-DAP 接口和端点；无线
  主机仍暴露完整的 MSC、CDC 和 CMSIS-DAP 组合设备。

LED 通道与 MCU GPIO 的实际对应关系以[硬件手册](hardware_manual.md)为准。
2026-08-27 实板低电平单色测试确认 `PC13=蓝`、`PC14=红`、`PC15=绿`。更新后的
EasyEDA 网络名分别为 `STAT_LED_B`、`STAT_LED_R`、`STAT_LED_G`，与实板发光颜色一致。

### 测试步骤

1. 分别给两块板烧录 `build/gcc/release/daplink_wireless.hex`。
2. 分别确认两块板的 `CONFIG.TXT`：主机为 `MODE=WIRELESS_HOST`，从机为
   `MODE=WIRELESS_SLAVE`；两块板的 `SYNC` 必须一致。保存后安全弹出并复位。
3. 首次上电使用限流电源，确认主机蓝灯闪烁、从机绿灯闪烁，且红灯熄灭。
4. 两块板相距至少 1 米，确认 `M20SX`/E28 外接天线已正确安装。
5. 按下 A 板按键；A 板蓝灯应短亮，B 板绿灯应短亮。
6. 按下 B 板按键，确认反向链路结果相同。
7. 重复至少 100 次，确认无红灯、失联或异常复位。

### 红灯闪烁时

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
