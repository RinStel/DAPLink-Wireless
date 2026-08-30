# Host 多事务状态模块

新增 `firmware/app/wireless_swd_pipeline.[ch]`，提供固定 2 槽 sequence 状态表：

- push 保存类型、payload 和 sequence；
- next_tx 支持首次发送及按超时重传；
- ACK 与 response 分离记录；
- response 按 sequence 顺序取出，避免乱序交付 USB；
- cancel 可清除指定事务。

该模块已加入 Release 构建并通过现有 33 项 Python 测试，但尚未接入 `serial_bridge` 主路径。接入前仍需补充 Remote sequence 映射、重复请求缓存和 Abort 测试。
