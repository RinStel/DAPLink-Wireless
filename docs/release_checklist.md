# 发布检查清单

## 软件门禁

- [x] CMSIS-DAP v2、无线协议、SX1281 驱动、配置、链路自适应、USB 描述符和磁盘测试通过。
- [x] GCC Debug 与 Release 使用 `-Wall -Wextra -Werror` 构建通过。
- [x] Keil 工程零错误、零警告构建通过。
- [x] Flash、RAM 和单函数静态栈占用设有构建门限。
- [x] 配置 Flash 双副本通过写入中断、提交失败、CRC 损坏和回退模拟。
- [x] ELF、HEX、BIN 及 SHA-256 清单可重复生成。
- [x] 连续两次 Release 构建的 ELF、HEX、BIN 和 manifest 字节一致。
- [x] 固件版本、协议版本和硬件版本写入发布清单。
- [x] 发布清单记录构建器版本和可独立复算的源码树 SHA-256 指纹。
- [x] CMSIS-DAP 由固定提交的 Git submodule 管理。
- [x] GD32 V3.0.3 作为厂商快照保存在 `vendor/`，目录树哈希已锁定。
- [x] Git 索引拒绝 PDF、构建产物、错误子模块形态和锁外厂商文件。
- [x] GitHub Actions 使用固定工具链执行无 Keil软件门禁并保存构建产物。
- [x] 项目适配代码不修改或复制依赖实现。
- [x] 发布包包含 GigaDevice 和 Arm CMSIS-DAP 的第三方许可与归属声明。

## 实机门禁

- [ ] 在真实 Cortex-M 目标上通过 Arm CMSIS-DAP Validation。
- [ ] 使用 Keil、pyOCD 和 OpenOCD 验证下载、断点、单步和复位。
- [ ] 验证目标持续返回 WAIT 时的 Transfer Abort 延迟。
- [ ] 验证两台设备从不同受阻信道启动后能够重新会合。
- [ ] 验证 GFSK/FLRC 和全部空中速率的切换与回退。
- [ ] 验证看门狗、掉电写入和异常断链恢复。
- [ ] 完成至少 24 小时无线调试与串口并发压力测试。
- [ ] 完成 Windows、Linux 和 macOS 的 USB 枚举及恢复测试。

## 发布合规

- [ ] 替换 GD32 示例 USB VID/PID，并确认分配和驱动发布方式。
- [ ] 确定项目许可证并添加顶层 `LICENSE`。
- [ ] 在正式 Git 仓库中创建带签名或可追溯提交的版本标签。
- [ ] 保存硬件版本、BOM、生产测试和校准记录。

仅当所有项目完成后才能将发布状态从 `release-candidate` 改为
`production`。
