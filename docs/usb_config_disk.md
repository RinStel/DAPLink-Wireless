# USB 配置盘

固件使用 GD32F303 的 USBD 外设枚举为一个 16 KiB FAT12 大容量存储设备。
USB 协议栈位于 `vendor/GD32F30x_usbd_library`，GD32 CMSIS 和标准外设库位于
`vendor/GD32_CMSIS` 和 `vendor/GD32F30x_standard_peripheral`。GCC 与 Keil
工程均直接引用 `vendor/` 目录下的官方库源码。

## 配置方法

打开虚拟磁盘中的 `CONFIG.TXT`，保留以下格式：

```text
SYNC=DAPLINKWIRELESS1
DEVICE_MODE=WIRELESS_HOST
MODE=AUTO
PROFILE=GFSK1M
```

- `SYNC`：必须是 16 个 ASCII 字母或数字。
- `DEVICE_MODE`：`WIRED`、`WIRELESS_HOST` 或 `WIRELESS_SLAVE`。
- `MODE`：`AUTO` 或 `FIXED`。
- `PROFILE`：`GFSK2M`、`GFSK1M`、`GFSK500K`、`FLRC1M3` 或
  `FLRC650K`。

保存后必须使用系统的“弹出”或“安全删除硬件”，确保主机发送缓存同步命令。
固件仅在对应 SCSI 命令的状态包发送完成后提交配置，不使用固定超时强制断开仍
处于挂载状态的磁盘。

缓存同步完成后，固件校验配置，断开 USB，再交替写入 MCU 最后两个
2 KiB Flash 页。记录包含递增序号、CRC32 和最后写入的提交标记，掉电时仍
保留上一份有效配置。GCC 和 Keil 工程均排除了最后 4 KiB。写入失败或文件
格式错误时，设备继续使用上一份有效配置。

`STATUS.TXT` 显示当前固件版本、上次配置应用结果和复位原因。配置成功或失败后
设备都会重新枚举配置盘，失败时 `CONFIG.TXT` 恢复为仍在使用的旧配置。

板载按键也可修改配置：短按依次切换自动速率和各固定空中速率；长按 2 秒依次
切换 `WIRED`、`WIRELESS_HOST`、`WIRELESS_SLAVE`。按键修改通过相同的双槽
Flash 事务保存，并触发配置盘重新枚举。

当前描述符沿用 GD32 官方示例 VID。产品化前必须申请或取得合法的 USB
VID/PID，不能直接以该标识量产。

USB 设备同时提供配置磁盘和 CDC ACM 虚拟串口。主机程序设置虚拟串口的
波特率、数据位、校验位和停止位时，固件通过标准 `SET_LINE_CODING` 请求
获得参数；无线主机会将参数同步给无线从机，因此不需要自动波特率探测。
