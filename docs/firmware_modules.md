# 固件模块边界

固件使用单主循环和非阻塞状态机，不使用 RTOS。

## 调度层

- `main.c`：初始化并调用 USB、无线和指示灯服务。
- `serial_bridge.c`：无线链路状态机和各业务服务之间的调度。

`serial_bridge` 不再解析帧字段，也不直接操作 CDC、UART 或 SWD GPIO。

## 协议与业务层

- `radio_protocol.c`：无线帧编解码、字段校验和重复帧键。
- `serial_service.c`：CDC/UART 参数转换和有线、无线串口透传。
- `swd_bridge_service.c`：SWD 请求生命周期、响应缓存、取消和重复回复。
- `swd_tunnel.c`：SWD 操作的无线负载格式及异步 `SWJ_Pins`。
- `cmsis_dap.c`：CMSIS-DAP 命令状态机。
- `link_adaptation.c`：RSSI EWMA、升降速投票和驻留时间。
- `frequency_hopping.c`：频道表、同步码派生排列和坏频道惩罚。

## 驱动层

- `sx128x.c`、`radio_hal.c`：SX1281 命令及 SPI/GPIO。
- `target_uart.c`：目标串口环形缓冲区。
- `target_swd.c`：SWCLK、SWDIO 和 NRST 时序。
- `board.c`：板级 GPIO、SysTick、DWT 时基和设备 ID。

## USB 与配置

- `usb_composite.c`：MSC、CDC、CMSIS-DAP v2 组合描述符。
- `cmsis_dap_usb.c`：CMSIS-DAP v2 Bulk 传输适配。
- `toolchain/gcc/syscalls.c`：无操作系统环境下的 Newlib 系统调用桩，仅供 GCC 链接。
- `usb_config_disk.c`：虚拟 FAT12 配置盘。
- `device_config.c`、`device_config_storage.c`：运行配置和 Flash 双槽持久化。
