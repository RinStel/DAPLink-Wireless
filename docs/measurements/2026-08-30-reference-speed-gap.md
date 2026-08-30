# SX1281 参考速度差异审计

## 结论

SX1281 仅提供物理层能力，不能解释 48 KB/s 与当前约 5.30 kB/s 的差距。当前无线路径的主要成本是逐请求固定往返：请求 ACK、远端 SWD 执行、响应发送和 USB IN。`RADIO_WINDOW_SIZE=4` 目前未形成有效流水线，因为 `serial_bridge` 的 `s_pending` 仍限制 SWD 请求一次只处理一个。

参考工程可见的可迁移点仅限 USB 侧：Bulk 64 字节、OUT 回调立即重新接收、环形缓冲和独立 IN busy。其公开内容没有可核对的 SX1281 无线协议源码，因此不能据此安全删除 ACK 或合并 SWD 语义。

## 当前证据

- 64 KiB pyOCD：约 5.30 kB/s。
- Burst 发送：0；容量拒绝：1993 次。
- ACK 平均等待：约 1232.69 µs。
- Remote 往返平均：约 1716.81 µs。
- 响应到 USB IN：约 734.53 µs。
- RF 重传率：0%。

## 本轮验证

- `SERIAL_BRIDGE_ACK_TURNAROUND_DELAY_US` 保持 50 µs，并修正对应单元测试。
- Release 固件构建成功，生成 `daplink_wireless.dwup` 和 `daplink_factory.hex`。
- Python 测试 33 项通过；现有 C 测试可执行文件全部通过。
- `pyocd list` 仅发现 1 个探针且 Target 为 `n/a`，未执行实机刷写，避免误写未知目标。

## 下一步

需要同时连接并确认 DAP 主机和 DAP 从机后，重复 64 KiB 测试。若 50 µs 稳定，再以编译宏单变量测试 25–30 µs；若无收益，应优先重构无线事务确认/批处理，而不是继续调整 USB 环形缓冲。
