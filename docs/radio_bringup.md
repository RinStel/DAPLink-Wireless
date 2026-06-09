# 双板射频冒烟测试

## 固件行为

- 绿灯约 1 Hz 闪烁：主循环与射频初始化正常。
- 红灯常亮：SX1281 复位、BUSY、SPI、配置回读或运行过程失败。
- 蓝灯短亮：本板开始发送诊断帧，或收到另一块板的有效诊断帧。

## 测试步骤

1. 分别给两块板烧录 `build/gcc/release/daplink_wireless.hex`。
2. 首次上电使用限流电源，确认两块板均红灯灭、绿灯闪烁。
3. 两块板相距至少 1 米，确保 `M20SX` 外接天线已经正确安装。
4. 按下 A 板按键。A 板蓝灯应短亮，B 板蓝灯也应短亮。
5. 按下 B 板按键，确认反向链路结果相同。
6. 重复操作至少 100 次，确认无红灯、无失联和异常复位。

## RSSI 回包与动态切换

每次按键通信包含以下过程：

1. 发送端使用当前 profile 发送 `PING`。
2. 接收端读取 SX1281 `GetPacketStatus`，获得 RSSI、错误状态、收发状态和同步字状态。
3. 接收端使用当前 profile 返回 `REPORT`，其中 RSSI 单位为 0.5 dBm。
4. 发送端根据报告发送 `SWITCH`，接收端回复 `SWITCH_ACK`。
5. 接收端在确认包发完后切换，发送端在收到确认后切换。
6. 若确认丢失，发送端轮询所有 profile 并重发探测包。

当前自适应阈值：

| 接收 RSSI | 推荐 profile |
| --- | --- |
| `>= -55 dBm` | GFSK 2 Mbps |
| `>= -68 dBm` | GFSK 1 Mbps |
| `>= -80 dBm` | FLRC 1.3 Mbps，CR 3/4 |
| `>= -90 dBm` | GFSK 500 kbps |
| `< -90 dBm` | FLRC 650 kbps，CR 3/4 |

检测到 CRC、同步、长度或中止错误时直接降至 FLRC 650 kbps。

## 当前无线参数

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
| SX1281 输出 | -2 dBm，对应 E28 外部 PA 约 20 dBm |

支持动态切换的 profile 为 GFSK 2 Mbps、GFSK 1 Mbps、GFSK 500 kbps、
FLRC 1.3 Mbps 和 FLRC 650 kbps。

## 若红灯常亮

依次检查：

1. `NRESET` 是否出现低脉冲并恢复高电平。
2. 复位后 `BUSY` 是否最终拉低。
3. SPI0 的 PA5/PA6/PA7 是否有波形，NSS 是否在 PA4。
4. SPI 模式是否为 Mode 0，时钟约 7.5 MHz。
5. MISO 是否返回非固定高电平或低电平。
6. 模块 3.3 V 电源在发射瞬间是否稳定。
