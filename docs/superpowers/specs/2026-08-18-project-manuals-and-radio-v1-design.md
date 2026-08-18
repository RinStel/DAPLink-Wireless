# 项目手册合并与无线协议 v1 设计说明

日期：2026-08-18

## 目标

将项目自有文档合并为少量可维护的项目手册，删除完成归并后不再具有独立阅读价值的旧文档；同时将无线链路协议版本从 `v4` 统一调整为 `v1`。

本次工作不修改 `FIRMWARE_VERSION_STRING`，当前固件发布候选版本仍为 `0.8.0-rc.3`。无线协议版本与固件发布版本是两个独立标识。

## 范围与边界

- 只整理根目录项目文档和 `docs/` 下的项目文档。
- 不改写 `Third-Party/CMSIS-DAP/**`、`LICENSE` 或厂商源码。
- 保留 `docs/schematic_u5_connections.md`；文件内容是 EasyEDA 原理图事实记录和固件校对依据。
- 不在本次文档合并中同步修改 U5 引脚映射；手册必须分别标注“原理图基线”“当前代码状态”和“待确认：”项。
- 原理图中的 `Board_V1.0`、页面版本 `V1.0` 与现有项目文档中的硬件版本 `v0.5` 的对应关系保持为“待确认：”，不得擅自改写为同一版本。

## 目标文档结构

| 文档 | 内容来源 | 责任边界 |
| --- | --- | --- |
| `README.md` | 现有根目录 README | 项目定位、快速构建、配置入口和文档入口 |
| `docs/README.md` | 现有文档索引 | 手册目录、文档用途和阅读顺序 |
| `docs/project_manual.md` | 产品架构、固件模块、USB 配置盘、开发路线 | 设备模式、固件分层、USB 配置和软件状态 |
| `docs/hardware_manual.md` | 硬件审查、硬件验收 | 器件、板级安全态、上电、实机验收和硬件风险；完整 U5 引脚表引用原理图记录 |
| `docs/wireless_manual.md` | 无线协议、跳频设计、射频启动流程 | 帧格式、可靠事务、速率/频道切换、参数和双板冒烟测试 |
| `docs/development_release_manual.md` | 开发任务、CMSIS-DAP 验证、发布检查、厂商依赖 | 软件回归、实机验证、发布门禁、依赖和发布输入 |
| `docs/schematic_u5_connections.md` | EasyEDA Bridge 读取记录 | U5 全部网络、跨页连接、代码映射差异和待确认事项 |
| `CHANGELOG.md` | 现有变更记录 | 版本历史，不并入项目手册 |
| `THIRD_PARTY_NOTICES.md` | 现有第三方声明 | 许可证和归属，不并入项目手册 |

## 删除清单

在新手册写入、链接检查和内容覆盖复核完成后，删除以下旧文档：

```text
docs/cmsis_dap_validation.md
docs/development_tasks.md
docs/firmware_modules.md
docs/frequency_hopping.md
docs/hardware_acceptance.md
docs/hardware_review.md
docs/product_architecture.md
docs/radio_bringup.md
docs/radio_protocol_v4.md
docs/release_checklist.md
docs/roadmap.md
docs/usb_config_disk.md
docs/vendor_dependencies.md
```

删除前必须确认项目自有 Markdown 和发布脚本中没有继续引用这些路径；上游文档、许可证、构建目录和 `docs/schematic_u5_connections.md` 不在删除范围内。

## 无线协议版本调整

无线帧头第 2 字节的协议版本由 `4` 改为 `1`，具体约束如下：

- `firmware/app/radio_protocol.h` 中的 `RADIO_PROTOCOL_VERSION` 改为 `1U`。
- `firmware/app/radio_protocol.c` 继续使用 `RADIO_PROTOCOL_VERSION` 写入和校验帧头，不新增并行版本常量。
- `tests/radio_protocol_test.c` 同时验证构造帧写入 `1`，并验证非 `1` 的版本被拒绝。
- `scripts/build_gcc.ps1` 和 `scripts/verify_release.ps1` 保持从头文件读取协议版本；Release manifest 的 `radio_protocol` 应自动变为 `1`。
- 新手册统一使用“无线链路协议 v1”和“协议版本 `01`”；旧的 `radio_protocol_v4.md` 在归并后删除。
- `CHANGELOG.md` 保留已公开版本的历史事实；当前 `0.8.0-rc.3` 尚未公开发布，因此候选版本条目按协议 v1 的最终状态记录，并在 `Unreleased` 说明本次调整。

协议版本调整会使版本为 `4` 的帧被当前代码拒绝，属于无线协议字段兼容性变化。当前项目未发布产品，因此不保留 v4/v1 双接受路径。

## 文档事实与措辞规则

- 原理图直接读取结果使用精确网络名、引脚号和 EasyEDA 标识符。
- 当前固件状态引用实际文件和宏名，例如 `firmware/bsp/board_pins.h`、`RADIO_PROTOCOL_VERSION`。
- 未由原理图、代码或测试证明的内容使用 `待确认：` 标记，并写明确认对象。
- 保留需求强度：`必须`、`不得`、`应`、`可以` 等词不降级为建议性描述。
- 单位、有效电平、超时、频率和字节偏移必须保留；多字节整数序和协议偏移不能只用自然语言概括。
- 技术术语统一使用“无线主机”“无线从机”“目标 SWD”“协议版本”“固件发布版本”“频道”。

## 验收标准

1. 新手册覆盖被删除文档中的有效架构、硬件、协议、验证、发布和依赖事实。
2. 项目自有文档的内部链接全部指向现存文件；不存在 `radio_protocol_v4.md` 或旧手册路径引用。
3. `rg` 检查项目代码和文档后，生产代码、测试和项目手册不再把当前无线协议描述为 v4。
4. `tests/radio_protocol_test.c` 能捕获错误协议版本，且主机测试通过。
5. GCC Debug/Release 构建与发布脚本的协议版本读取逻辑保持一致；Release manifest 的 `radio_protocol` 为 `1`。
6. cste-zh 术语、编号、表格、命令、链接和 `待确认：` 标记通过检查。
7. Git 差异中只包含本次手册合并、协议版本调整和明确删除清单，不覆盖用户已有的 `docs/schematic_u5_connections.md` 内容。
