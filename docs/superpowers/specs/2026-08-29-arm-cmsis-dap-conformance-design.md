# Arm CMSIS-DAP 一致性修复设计

## 目标

- 以 `vendor/Arm-CMSIS-DAP` 中固定提交的 CMSIS-DAP 2.1.2 实现作为协议判定基准。
- 补齐 `DAP_QueueCommands (0x7E)` 和 `DAP_ExecuteCommands (0x7F)`。
- 对 `DAP_Transfer`、`DAP_TransferBlock` 和 SWD 边界执行差分测试。
- 恢复已完成的无线通信链路修复，不增加私有 Keil 追踪协议。

## 已确认的差异

Arm USB 模板会将 `DAP_QueueCommands` 转为延后执行的
`DAP_ExecuteCommands`。`DAP_ExecuteCommand()` 会按子命令实际消耗的请求长度
推进输入，并连接每条子命令的响应。当前项目只处理单命令包，`0x7E` 和 `0x7F`
均返回 `DAP_Invalid`。Keil 可以在编程阶段使用批量命令，因此该差异与
`Erase Done` 后 `Programming Failed` 的边界一致，但仍需实机验收确认因果关系。

## 实现边界

生产固件继续使用协作式异步 CMSIS-DAP 状态机。Arm 官方 `DAP.c` 是同步实现，
不能直接替换无线主机状态机，否则等待远端 SWD 时会阻塞射频调度。

主机测试将官方 `DAP.c` 编译为协议判定器，并使用确定性的 SWD 后端。项目实现
对相同命令产生的响应字节和 SWD 调用顺序必须与判定器一致。生产代码可以复用
官方命令标识和算法结构，但异步推进、超时和取消仍由项目适配层负责。

`DAP_ExecuteCommands` 的生产实现必须：

- 保留外层命令和声明的命令数量；
- 按每条子命令的实际请求长度推进输入；
- 等待异步子命令完成后再提交下一条；
- 按官方顺序连接完整子响应；
- 在 64 字节请求或响应边界外拒绝畸形包；
- 保持单活动 CMSIS-DAP 命令和 USB 响应顺序。

USB 传输层必须：

- 将 `DAP_TransferAbort` 作为带外取消处理；
- 暂存 `DAP_QueueCommands`，直到后续请求到达；
- 执行时只把首字节从 `0x7E` 改为 `0x7F`，其余内容保持不变；
- 保持四槽请求和响应环，不增加新的 USB 接口或 Vendor 命令。

## 无线链路

恢复 1.0.19 已完成的通信链路行为：无线协议 v3、固定 profile 紧凑 ACK、
固定 profile 省略 packet status、下一条 SWD 请求隐式确认上一响应，以及
12–19 ms 的 SWD 请求 ACK 重试窗口。标准 CMSIS-DAP v2 GUID、200 us ACK
转向保护、4 MHz SWD 上限和 Arm SWD 收尾修复保持不变。

## 验证

- 官方差分测试覆盖普通命令、`DAP_Transfer`、`DAP_TransferBlock`、AP posted
  read、写后 `DP_RDBUFF`、WAIT、FAULT 和 Match Value。
- 聚合命令测试覆盖两个同步子命令、同步后异步子命令、多个异步子命令、请求
  截断和响应溢出。
- USB 测试覆盖 Queue 延后执行、后续请求触发和 Abort 带外处理。
- 完整主机测试、Debug/Release GCC 构建和发布门禁必须通过。
- 双板必须报告同一新版本，并保持 `WIRELESS_SLAVE/HOST + FLRC1M3`。
- 最终结果以 Keil 实际 Flash Download 为准；pyOCD 成功不能替代该验收。

