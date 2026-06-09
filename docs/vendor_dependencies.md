# 官方依赖与发布输入

## 已纳入工程

- GD32F30x CMSIS V3.0.3。
- GD32F30x 标准外设库 V3.0.3。
- GD32F30x USB Device 库。
- GD32F303xx 数据手册 Rev3.3 和用户手册 Rev3.4。
- Semtech SX1280/SX1281 数据手册 V3.3。
- E28-2G4M20S 用户手册 V1.6；E28-2G4M20SX 电气和软件兼容。
- Arm CMSIS-DAP，基线提交 `6256803b7ac93731ec22e24e0ae8d91df3a7c953`。

这些厂商 PDF 仅作为本地设计参考，不提交到 Git 仓库。开发者需自行从
厂商官方网站获取对应版本。

`GD32F30x_usbfs_library` 不属于项目依赖，也不保留在工作区。固件只使用
依赖锁列出的 `GD32F30x_usbd_library`。

工程使用独立的异步 CMSIS-DAP 前端，以适配无线请求模型。
`Third-Party/CMSIS-DAP` 由 Git submodule 管理并固定到上述提交，仅作为协议
基线和官方 Validation 工程，不直接编入固件。

GD32F30x V3.0.3 没有使用 submodule，作为 GigaDevice 官方发布包快照保存在
`vendor/`。项目代码不得修改这些文件；`dependencies.lock.json` 记录三个参与
构建的 GD32 目录树哈希，发布验证会拒绝任何未更新锁文件的变化。

## 正式发布前仍需提供或确认

- 可合法用于产品发布的 USB VID/PID。
- 项目顶层许可证选择。
- 一套真实 Cortex-M 目标板，用于 Arm Validation、Keil、pyOCD 和 OpenOCD。
- 两块完整无线调试器硬件，用于跳频、吞吐、掉电和长稳验证。
- 若需要量产，需提供生产测试接口、射频法规目标地区和校准要求。
