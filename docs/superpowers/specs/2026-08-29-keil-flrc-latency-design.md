# Keil 枚举与 FLRC 延迟修复设计

## 目标

- 让 Keil 通过标准 CMSIS-DAP v2 设备接口 GUID 枚举无线 DAP。
- 记录 USB、CMSIS-DAP、FLRC、无线从机和目标 SWD 的完整请求/响应链路。
- 消除固定 FLRC profile 下周期出现的约 625 ms 长尾，并将内部无线协议迭代为 v3。
- 将当前固件版本更新为 1.0.18；旧无线固件不兼容，主从机必须同时升级。

## 已确认的根因

固件当前把 WinUSB 接口注册为自定义 GUID
`{7E5A8D6B-4F72-4A3C-9B16-2D8C1E0F5347}`。Arm CMSIS-DAP v2 模板使用
`{CDB3B5AD-293B-4663-AA36-1AAE46463776}`。pyOCD 可以按 USB 属性找到设备，
Keil 的 CMSIS-DAP 驱动按设备接口类枚举，因此 Keil 列表为空。

FLRC1M3 下连续执行 200 次 `DAP_SWJ_Clock` 的实测中位数约为 4.0 ms，
但每隔约 32 次出现一次约 625 ms 长尾。主机每收到 32 个 ACK 后设置一次
`s_hop_request_pending`。下一次由主机发送的 ACK 携带 `HOP_VALID`，从机收到后
切换频道；主机发送该 ACK 后没有在 `TX_DONE` 中切换，双方因此短暂失步，并走
600 ms 级 SWD ACK 超时/恢复路径。

## 设计

USB 描述符使用 Arm 标准 CMSIS-DAP v2 GUID，保留 WinUSB、接口号 3、端点
`0x05/0x85`、64 字节包长和现有 VID/PID。

v3 保留 17 字节通用帧头和 110 字节应用负载上限，但 ACK 在固定 profile 下允许
使用 8 字节紧凑布局；AUTO profile 和携带跳频信息时仍使用完整 ACK。SWD 请求
继续使用到达 ACK，将“请求未到达”和“目标 SWD 尚未完成”分阶段确认；ACK
转向保护保持为已验证的 200 µs。`ack_send()` 选择下一频道并设置 `HOP_VALID` 时，同时记录
`s_channel_after_ack` 和 `s_channel_switch_after_ack`。ACK 接收方仍在解析 ACK 后
立即切换；ACK 发送方在该 ACK 的 `TX_DONE` 后切换。ACK 未成功提交时清除待切换
状态，避免单边切换。

周期跳频只由无线 DATA 的成功传输推进。健康的 SWD 会话不触发周期跳频；
SWD 实际超时后仍使用现有失败惩罚和恢复频道选择机制。当前实现没有扫描全部
信道，也没有用 RSSI 对候选信道排序，因此本文将其称为“失败信道回避”，不得称为
“选择更优信道”。

健康链路空闲后，主从机只回到并停留在同步码派生的 rendezvous 频道。不得按
各自的 `last_valid_rx` 时基持续自由扫描候选频道；两个本地时基存在偏差时，该
行为会让下一次 Keil/RDDI SWD 事务先经历 600 ms 级超时。事务实际超时后的可靠
重试仍可以使用恢复频道选择。

链路文档写入 `docs/wireless_manual.md`，明确队列、状态机、帧类型、ACK 语义、
超时和正常时延组成。文档必须区分中位时延、长尾、软件测试和实机验收。

Keil 失败后的 Vendor Status 必须跨 USB 类重新初始化保留。版本 7 记录 USB Bulk
OUT、CMSIS-DAP 核心提交和 USB 类生命周期边界。Vendor Status 查询不得更新自身
计数。计数用于定位故障边界，不替代 Keil 实际 Flash Download 验收。

Vendor `0x81` 保存最近 9 条非诊断响应摘要、最后一条错误响应，以及核心生成、
USB 入队、Bulk IN 启动和 Bulk IN 完成计数。Vendor `0x80` 和 `0x81` 查询不得
污染响应追踪。该追踪只用于定位合法 CMSIS-DAP 响应边界，不改变现有命令语义。

## 验证

- 描述符主机测试必须逐字节检查标准 GUID。
- 跳频调度测试必须检查：无请求不切换；同频道不切换；不同频道在 ACK 后安排
  `TX_DONE` 切换。
- 完整主机测试、Debug/Release GCC 构建和发布门禁必须通过。
- 主从机必须报告 1.0.18、配置有效并保持 `FLRC1M3`。
- 200 次 FLRC 控制命令复测不得再出现周期性约 625 ms 长尾。
- Keil 必须在指定工程中枚举该 DAP，并以实际 Flash Download 结果验收。

软件门禁不替代无线时延和 Keil 实机验收。
