# 无线多事务流水线设计 v1

## 目标

在保持 CMSIS-DAP USB 命令边界、SWD posted-read/WAIT/FAULT/Abort 语义和 Keil 兼容性的前提下，将无线 SWD 从单事务停等改为有限发送窗口，目标是显著降低每个 block 的 ACK/转向固定开销。

## 状态模型

Host 为每个无线 SWD 请求分配单调 sequence，最多同时挂起 `PIPELINE_WINDOW_SIZE` 个 block。Remote 按 sequence 接收并排队，严格按序调用现有 SWD block executor；执行完成后将 response 放入有序响应 FIFO。累计 ACK 只确认“帧已接收并进入 Remote 队列”，不代表 SWD 已完成。响应帧单独携带 `response_sequence`，Host 按序交付 USB。

## 必须保持

- 每个 block 独立执行和收尾，不合并 SWD transfer。
- 重传只重发未被累计 ACK 确认的帧；Remote 对重复 sequence 只重发缓存响应，不重复写目标。
- Abort 到达时取消未执行队列，并返回明确失败响应。
- 队列溢出、响应乱序、超时或协议错误立即回退到现有 stop-and-wait 路径。
- 默认开关关闭，实验版本和稳定版本可通过编译宏区分。

## 实施顺序

1. 为 `serial_bridge` 增加 TX/RX pipeline slot 和累计 ACK 状态，不改变默认调度。
2. 为 `swd_bridge_service` 增加 Remote pending FIFO 与 response FIFO。
3. 增加序号/重复/乱序/Abort/超时单元测试。
4. 在诊断构建导出窗口深度、队列峰值、响应乱序和回退计数。
5. 双板实验版本刷写；先做 CPUID/IDCODE、重复 block、Abort，再做 64 KiB。

当前 `PIPELINE_WINDOW_SIZE` 初始值建议为 2，确认稳定后再测试 4；不得直接放开到无限队列。
