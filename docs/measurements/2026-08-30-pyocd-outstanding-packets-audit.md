# pyOCD Outstanding Packet 审计

当前安装的 pyOCD `pydapaccess/dap_access_cmsis_dap.py` 在 `_send_packet()` 中允许最多 `interface.get_packet_count()` 个未读命令；达到上限才读取响应。也就是说，主机具备最多 4 个 CMSIS-DAP packet 的发送窗口。

当前固件没有利用这一能力：`cmsis_dap_usb_process()` 只有在 `!cmsis_dap_busy()` 时才从 USB 请求环提交命令，而 `cmsis_dap_busy()` 在任何异步 SWD 事务期间都返回 true。结果是主机可以发送多个 packet，但固件只取第一个，后续请求无法进入 Burst/FIFO。

这确认下一阶段的首个代码改动应是 CMSIS-DAP 核心的原始命令 FIFO（至少 4 槽），并在当前异步事务完成/响应取走后按顺序启动下一条。只有此 FIFO 建立后，已有 USB 请求环和 Burst 聚合路径才有机会获得真实输入。

该改动必须独立处理非幂等 Flash 写入、Abort 和响应顺序，不能简单移除 `cmsis_dap_busy()` 判断。
