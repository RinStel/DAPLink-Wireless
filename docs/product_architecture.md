# 产品固件架构

## 设备模式

- `WIRED`：USB CDC 与目标 USART0 直接透传。
- `WIRELESS_HOST`：USB CDC 与 SX1281 无线链路透传。
- `WIRELESS_SLAVE`：SX1281 无线链路与目标 USART0 透传。

PA9 为 `TGT_TXD`，PA10 为 `TGT_RXD`。默认串口参数为 115200-8-N-1。

## 波特率

不使用自动波特率探测。PC 打开 CDC ACM 虚拟串口时会发送
`SET_LINE_CODING`，其中包含波特率、数据位、校验位和停止位。

- 有线模式立即将参数应用到 USART0。
- 无线主机通过可靠控制帧发送参数。
- 无线从机应用参数后回复 ACK。

这种方式不依赖目标串口发送训练字符，也不会因二进制数据内容误判波特率。

## 无线串口协议

无线主机拥有控制权，从机不主动修改链路配置。串口数据和行参数使用递增序号、
ACK、超时重传与重复帧抑制。当前单帧最多承载 64 字节，采用停等模型，优先保证
数据顺序和正确性，后续再增加滑动窗口提高吞吐量。

## USB

USB FS 枚举为 MSC + CDC ACM + CMSIS-DAP 组合设备：

- MSC 暴露 `CONFIG.TXT`。
- CDC ACM 提供虚拟串口。
- CMSIS-DAP v2 使用接口 3 的 64 字节 Bulk OUT/IN 端点。

USB OUT 使用背压保证只存在一个未完成 DAP 命令。Windows 8 及以上可通过
Microsoft OS 1.0 WCID 将 v2 接口自动绑定到 WinUSB。

USBFS 只有 512 字节 PMA。EP0 使用合法的 8 字节最大包，CDC 数据端点使用
16 字节包，DAP v2 端点保持 64 字节。

量产前必须替换 GD32 示例 VID/PID，并完成 Windows、Linux 和 macOS 的枚举与
休眠恢复测试。

## SWD 传输

PB6 为目标 `SWCLK`，PB7 为目标 `SWDIO`，PB4 为目标 `NRST`。当前实现采用
目标端本地 GPIO 位操作，无线链路不传输逐 bit 时序：

- `CONNECT` 执行 SWJ line reset、JTAG-to-SWD 序列并读取 DP IDCODE。
- `TRANSFER` 每帧最多批量执行 10 个 DP/AP 读写，WAIT 在目标端本地重试。
- `RESET` 以开漏方式拉低目标 NRST 20 ms，随后释放引脚。
- 无线请求使用可靠帧，重复序号不会再次执行写操作或复位，只重发缓存结果。
- 有线模式通过同一服务接口直接执行，便于后续 CMSIS-DAP 前端复用。

当前前端已映射 `DAP_Info`、`DAP_Connect`、`DAP_Disconnect`、
`DAP_Transfer`、`DAP_TransferBlock`、`DAP_WriteABORT`、
`DAP_SWJ_Clock`、`DAP_SWJ_Sequence`、`DAP_SWD_Configure` 和
`DAP_SWD_Sequence`、`DAP_ResetTarget`。Transfer 支持 Match Value 与
Match Mask。JTAG、SWO 和时间戳尚未实现，能力位不会声明这些功能。

`DAP_TransferAbort` 使用独立 USB OUT 缓冲作为带外命令，不占用响应包。无线
SWD 操作按 transaction ID 过滤；CMSIS-DAP 超时或中止会取消可靠层中的对应
请求，迟到响应仅被链路 ACK，不会进入后续调试事务。

项目定义了诊断命令 `DAP Vendor 0x80`，响应格式版本为 5：

- 字节 1：格式版本。
- 字节 2：设备模式。
- 字节 3：bit0 无线就绪、bit1 错误、bit2 SWD 请求活动。
- 字节 4：当前无线重试次数。
- 字节 5..8：无线恢复次数。
- 字节 9..12：SWD 取消次数。
- 字节 13..16：迟到 SWD 响应次数。
- 字节 17..20：目标 UART 接收溢出次数。
- 字节 21..22：对端 RSSI，单位 0.5 dBm。
- 字节 23..24：当前 profile 与 profile 切换次数。
- 字节 25..27：对端 SX1281 包状态。
- 字节 28..29：当前频道索引与频道切换次数。
- 字节 30：上次复位原因位图。
- 字节 31..34：本次启动运行时间，单位 ms。
- 字节 35..38：射频 RX/TX timeout IRQ 次数。
- 字节 39..42：协议校验失败的射频帧数。
- 字节 43..46：检测到的对端 session 变更次数。
