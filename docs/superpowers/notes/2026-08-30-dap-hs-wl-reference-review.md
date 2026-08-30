# DAP_HS_WL 外部方案对照记录

## 来源

- 仓库：`https://github.com/ylj2000/DAP_HS_WL`
- HEAD：`e08ebfbaf82d6e39d4ca11a539d82b96044bcf34`
- 本地只读副本：`Third-Party/DAP_HS_WL`

## 当前可确认事实

仓库公开文件主要是 PDF、硬件工程压缩包、工具压缩包和固件压缩包。README 只说明：

- 无线调试器使用 `Fireware/Ver_01/DAP.7z` 中的固件；
- 不安装无线模块时使用 `DAP_None_WL.7z` 中的有线固件。

仓库没有可直接审阅的 C 源码、无线帧格式、ACK 时序、CMSIS-DAP 缓冲策略或吞吐测量数据。当前环境没有 7-Zip 解包器，因此不把压缩包内未知实现推断为协议事实。

## 与本项目当前实现的可比边界

| 项目 | DAP_HS_WL 公开资料 | 本项目已验证实现 |
| --- | --- | --- |
| CMSIS-DAP 接口 | 未说明 | CMSIS-DAP v2 Bulk，EP5，64 字节最大包 |
| 无线协议 | 未公开 | 带网络 ID、会话、序号、可靠 ACK、重复缓存 |
| SWD 批量 | 未公开 | `SWD_BLOCK`；Burst v1 已验证因双向 110 字节容量无法装入真实 pyOCD 大块 |
| Keil 兼容性 | 未提供日志 | `DAP_Info(0xF0)` 默认不声明 Atomic Commands，Keil 已实机通过 |
| 吞吐数据 | 未提供 | v3 单发约 5.30 kB/s；主要固定成本为 ACK、远端往返和 USB IN |

## 可采纳方向

1. 如果解包后证实其使用更大的无线数据帧或单向流水线，可作为“提高无线有效载荷/减少 ACK 次数”的候选设计；必须先确认 SX1281 配置、CRC、重传和 Keil 错误处理。
2. 如果其 USB 端使用更大的主机缓冲，应只借鉴主机侧分段/组包思路；本项目 USB FS Bulk 端点、PMA 和 CMSIS-DAP 包长仍受 64 字节限制，不能直接把 `DAP_INFO_PACKET_SIZE` 改为 512/1024。
3. 如果其固件通过固定窗口发送多个 SWD 命令，必须单独验证 AP posted-read、`DP_RDBUFF`、WAIT/FAULT、写后检查和 Abort 语义；不能把多个 transfer 数组直接拼接执行。

## 暂不采纳

- 不从压缩固件反推并复制未知协议；
- 不因“高速”项目名称修改 FLRC profile、SWD 时钟或 ACK 保护窗口；
- 不重新默认声明 Atomic Commands；
- 不把未提供的 DAP_HS_WL 性能数字写入项目验收结论。

## 下一步

优先获得可审阅源码或抓包/逻辑分析数据，再与本项目记录的 9 个时间点逐段对比。当前最有证据的优化方向仍是 ACK 转向保护窗口和无线帧有效载荷，而不是继续扩大无法装入 110 字节的 Burst。
## DAPLink_X033 对照补充

已审阅 `Third-Party/DAPLink_X033`（只读参考，未接入当前构建）。其可迁移的 USB 传输要点是：OUT 回调完成后立即重新启动接收、请求数据进入环形缓冲；IN 发送使用独立 busy 状态，允许发送期间继续排队响应。当前 `firmware/usb/cmsis_dap_usb.c` 已采用同等策略（请求/响应环、`receive_arm_if_space()`、`tx_busy`），因此没有再引入 X033 的协议或端点改动。

X033 使用有线 Bulk/HID 64 字节端点，不包含本项目 FLRC 无线 ACK/转向逻辑，不能直接据此压缩无线帧或删除 SWD 延迟。当前继续验证单变量 `SERIAL_BRIDGE_ACK_TURNAROUND_DELAY_US=50U`；收益和稳定性必须由实机重复烧录确认。
