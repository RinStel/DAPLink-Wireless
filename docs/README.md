# 项目文档索引

建议先阅读[项目手册](project_manual.md)，再根据工作内容进入对应手册。

## 项目手册

- [项目手册：产品、固件与 USB 配置](project_manual.md)
- [硬件手册：原理图、板级安全与验收](hardware_manual.md)
- [无线手册：协议 v2-only、跳频与射频验证](wireless_manual.md)
- [开发与发布手册：验证、依赖和门禁](development_release_manual.md)

## 原理图与版本事实

- [U5 原理图连接记录](schematic_u5_connections.md)：EasyEDA 读取的完整 U5 网络、跨页连接、当前代码差异和 `待确认：` 项。
- [变更记录](../CHANGELOG.md)：固件发布候选版本和未发布变更历史。

## 归属与许可证

- [第三方声明](../THIRD_PARTY_NOTICES.md)
- [项目许可证](../LICENSE)

## 版本说明

固件发布版本由 `FIRMWARE_VERSION_STRING` 表示，当前为 `1.0.0`；无线帧
协议版本由 `RADIO_PROTOCOL_VERSION` 表示，当前为 `2U`；接收端拒绝 v1 和未知版本，
发送端不保留 v1 路径。两者不可互换。

第三方 CMSIS-DAP 文档位于 `Third-Party/CMSIS-DAP/**`，属于上游资料，不在本项目
手册中重写。
