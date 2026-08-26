# Throughput and Wireless Protocol v2 Design

## 状态与范围

本设计针对当前 `firmware/` 第一方代码，覆盖有线 SWD、USB CDC、目标 UART、
无线桥接、SX1281 驱动和跳频控制。`vendor/` 与 `Third-Party/` 不修改。

当前工作区已有的有线 CMSIS-DAP 四包请求/响应环保持不变。本设计解决剩余的
SWD 时序、无线吞吐、同步阻塞、UART 轮询、CDC 包长、跳频和 SX1281 命令开销。

硬件验收仍是独立门槛。软件测试、构建和协议测试不得替代真实 SWCLK、烧录、
UART 压测和无线吞吐测量。

## 目标

- 有线 SWD bit-bang 热路径不再依赖可避免的函数调用和通用 GPIO 初始化。
- 一个无线 SWD block 只生成一个 v2 `SWD_BLOCK` 请求。
- 无线 `DATA` 使用双向有限窗口和累计 ACK，不再对每个业务帧停等 ACK。
- 长 SWD 请求由可取消的协作式状态机推进，不独占主循环。
- USART0 使用硬件 DMA/中断搬运，主循环只处理环形缓冲。
- MSC 保留；将 MSC bulk 端点降为 16 字节，释放 PMA 给 64 字节 CDC 数据端点。
- SX1281 只在状态变化时发送配置命令，并减少每个数据包的控制事务。
- 无线协议只支持 v2；v1 帧直接拒绝。

## USB PMA 方案

USB 全速设备保留 MSC、CDC ACM 和 CMSIS-DAP 三个功能。

| 端点 | 最大包长 | PMA 区域 |
| --- | ---: | --- |
| EP0 TX/RX | 64 / 64 | 现有控制端点区域 |
| MSC IN/OUT | 16 / 16 | 两个 16 字节区域 |
| CDC DATA IN/OUT | 64 / 64 | 两个 64 字节区域 |
| CDC notification IN | 8 | 8 字节区域 |
| CMSIS-DAP IN/OUT | 64 / 64 | 两个 64 字节区域 |

MSC 的逻辑介质块仍为 512 字节；只有 USB bulk 最大包长降低为 16 字节。因此
配置盘继续枚举，但配置盘吞吐不作为无线或烧录性能指标。

## 无线 v2 线格式

帧头固定为 17 字节，字段按大端编码：

```text
magic[2] | version[1] | type[1] | network_id[4] |
session[4] | sequence[4] | payload_length[1]
```

- `magic` 保持现有值。
- `version` 必须为 `2U`。解析器对 `version != 2U` 的帧返回拒绝，包含 v1。
- `payload_length` 必须等于实际帧长度减去 17。
- SX1281 最大物理负载为 127 字节，因此 v2 应用负载上限为
  `127 - 17 = 110` 字节。
- v2 不保留 v1 发送路径、v1 解析路径、双栈、协商、回退或迁移握手。

### DATA 窗口

- 每个方向独立维护 32 位序号空间和 4 个发送槽位。
- `DATA` payload 是透明业务字节，不重复携带长度；长度由帧头给出。
- 接收端最多缓存 4 个窗口内的乱序帧。只有连续序号到达后才向业务层投递。
- 重复帧不得重复投递，但必须触发当前累计 ACK。
- 超出接收窗口的帧丢弃并发送当前 ACK。

### ACK 格式

`ACK` payload 固定为 17 字节：

```text
ack_next[4] | bitmap[4] | flags[1] | next_channel[1] |
rssi_dbm_x2[2] | error_status[1] | tx_rx_status[1] |
sync_address_status[1] | profile[1]
| current_channel[1]
```

- `ack_next` 是接收端尚未连续收到的最小序号。
- `bitmap` 的 bit `i` 表示 `ack_next + i` 已收到，支持乱序确认。
- 发送端释放所有序号小于 `ack_next` 的槽位，并释放 bitmap 指示的槽位。
- `flags` 的 `ACK_FLAG_HOP_VALID` 置位时，`next_channel` 才有效。
- ACK 本身不占用 DATA 窗口；丢失时由后续 ACK 或发送端超时重传恢复。

控制帧继续使用单帧可靠重试。控制帧不得阻塞已经确认的 DATA 槽位，但
`SWD_BLOCK` 仍限制为一个活动事务，因为目标 SWD 引擎只有一个所有者。

### SWD_BLOCK

`SWD_BLOCK` payload 格式为：

```text
transaction_id[1] | count[1] | request[count] | write_data[4 * write_count]
```

`request` 按原始 SWD request 字节顺序排列。`write_data` 只包含写请求的数据，
并按对应写请求顺序排列。v2 应在 110 字节上限内接受最多 21 个普通 transfer。

`SWD_BLOCK_RESPONSE` payload 格式为：

```text
transaction_id[1] | completed[1] | ack[1] | read_count[1] | read_data[4 * read_count]
```

`read_data` 只包含读请求结果。`SWD_ABORT` payload 只包含 `transaction_id`，并
拥有高于普通 SWD block 的发送和处理优先级。

## 协作式 SWD 执行器

`swd_tunnel.c` 不再在一次 `swd_tunnel_process()` 调用中完成整个 transfer block。
新增内部执行状态：请求解码、当前 transfer、WAIT 重试、响应编码和完成/取消。

`target_swd` 提供以下异步接口：

```c
bool target_swd_transfer_begin(uint8_t request, uint32_t write_data);
target_swd_poll_result_t target_swd_transfer_poll(uint32_t *read_data);
void target_swd_transfer_cancel(void);
```

`target_swd_transfer_poll()` 每次只执行一个有界步骤。WAIT 重试达到 250 ms 或
配置的重试次数后返回超时；取消后不得再驱动目标数据阶段。现有同步调用只保留
为内部初始化和主机回归测试所需的薄封装，不作为无线请求执行路径。

## UART 与 CDC 数据流

- USART0 RX 使用循环 DMA，DMA 写位置通过剩余传输计数提交到软件环；TX 使用
  非循环 DMA 分段发送，完成后提交下一段。
- USART0 错误、DMA 错误和软件环溢出均计数。
- 主循环不读取 `RBNE/TBE` 状态位来搬运业务字节；USART IDLE 和 DMA 中断只负责
  发布位置及推进分段。
- CDC 数据端点为 64 字节；CDC 传输层保留独立的 RX/TX 软件环，避免 USB 回调
  直接阻塞 UART。

## 跳频与 SX1281 命令预算

- 默认每 32 个累计 DATA 序号计算一次候选频道。
- 下一频道通过 ACK 携带；不再为每次跳频额外发送 `HOP_SWITCH/HOP_CONFIRM`。
- 切换时保留旧频道短暂接收窗口，用于处理 ACK 丢失。
- SX1281 驱动缓存当前 packet type、profile、packet params 和 IRQ 配置。
- `SET_PACKET_PARAMS` 只有长度或调制参数变化时才发送。
- 发送路径不再无条件先发送 `STANDBY`；驱动根据当前状态直接提交 `SET_TX`。
- RX/TX 只清理实际需要的 IRQ 位，并使用 DIO1 电平作为主循环事件门控。
- 必要的 FIFO 状态、FIFO 数据和链路指标读取保持不变。

## 错误、取消与恢复

- v1 或格式非法的帧在协议层拒绝，不产生业务 ACK。
- DATA 窗口超时只重传未确认槽位；达到最大重试后报告链路错误并清空窗口。
- `SWD_ABORT` 清除本地 SWD 状态，并可靠发送到远端；远端确认取消后才释放
  事务所有权。
- 跳频切换失败时回到 rendezvous 频道并重新发送 v2 `SESSION_START`。
- UART 溢出不得覆盖未读数据，保留计数并向状态接口报告。

## 验证要求

主机测试必须覆盖：

- v2 编解码、v1/未知版本拒绝、110 字节边界和非法长度。
- ACK 累计确认、bitmap 乱序、重复 DATA 抑制、窗口满和超时重传。
- SWD_BLOCK 读写压缩、21 transfer 边界、Abort 和分步 WAIT。
- PMA 地址不重叠、MSC 16 字节端点、CDC/DAP 64 字节端点和描述符一致性。
- UART 环形缓冲、DMA 写指针推进、TX 分段和溢出计数。
- SX1281 命令缓存命中/失效以及 DIO1 事件门控。

Release 验收必须另外记录 1 MHz、2 MHz、4 MHz SWCLK，USB/无线吞吐、UART 3 Mbps
持续压测、CRC/readback 和硬件 Abort 恢复。没有硬件时不得将这些项目标记为通过。
