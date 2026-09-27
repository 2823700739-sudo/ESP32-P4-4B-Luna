# R1 USB P0 实机验证

本阶段只验证一件事：Luna 能通过原生 USB OTG 稳定地成为 Windows 外设。现有 Wi-Fi HTTP 卡片链路保持不变，等 USB 基础链路通过后再迁移业务数据。

## 接线

初次验证建议同时连接两根 USB 线：

- `USB TO UART`：继续负责固件烧录和串口日志。
- 原生 `USB OTG`：负责 Luna Link 数据通信，Windows 应把它枚举为 USB CDC 串口。

不要把两个接口混为同一个 COM 口。Windows 探针不写死端口号，会先筛选 Espressif `VID 303A / PID 4001`，再通过 Luna Link 握手确认设备身份。

## 构建与烧录

在仓库根目录运行：

```powershell
.\scripts\build-firmware.ps1 -Action reconfigure
.\scripts\build-firmware.ps1 -Action build
.\scripts\build-firmware.ps1 -Action flash-full -Port COM27
```

如果烧录口不是 `COM27`，请替换成设备管理器中 `USB TO UART` 对应的端口。烧录完成后继续用该端口看日志：

```powershell
.\scripts\build-firmware.ps1 -Action monitor -Port COM27
```

预期日志包含 `Luna Link USB CDC initialized`。诊断页会新增 `USB` 状态行。

## Windows 探针

首次运行或依赖变化后执行：

```powershell
.\pc-agent\setup-agent.ps1
```

保持探针运行，它会自动等待设备、完成握手、每两秒发送一次 PING，并在断线后重新发现：

```powershell
& .\pc-agent\.venv\Scripts\python.exe .\pc-agent\luna_usb_probe.py
```

只验证一次握手和 PONG：

```powershell
& .\pc-agent\.venv\Scripts\python.exe .\pc-agent\luna_usb_probe.py --once
```

成功后会看到 `Connected to Luna` 和 `PONG ... latency=... ms`。打开 Luna 的硬件诊断页，点击 `Touch / USB test`，探针应收到 `TOUCH {"touch_count":...}`。

## 验收记录

| 检查项 | 通过标准 | 实测结果 |
| --- | --- | --- |
| Windows 枚举 | 插入 OTG 后出现 Luna CDC 串口 | 2026-09-27：通过，COM28 / 303A:4001 / LUNA-P0 |
| 自动发现 | 未指定 COM 也能完成 Luna 握手 | 2026-09-27：通过，识别 R1-USB-P0 |
| 双向通信 | 连续 PING/PONG，无 CRC 或超时错误 | 2026-09-27：通过，连续 40+ 次 PING/PONG |
| 触摸上行 | 每次点击只收到一个递增事件 | 待实测 |
| 热插拔 | 连续拔插 20 次均能自动恢复 | 待实测 |
| 冷启动 | Windows 已开机时给 Luna 上电可恢复 | 待实测 |
| 探针重启 | Luna 不重启，探针重启后可重新握手 | 2026-09-27：通过，多次重启探针均重新握手 |

本地编译成功只能证明接口和依赖能通过构建，不能代替上述硬件结果。完成这些记录后，R1 下一步才把卡片状态和控制命令从 Wi-Fi HTTP 迁移到 USB。

## 协议摘要

Luna Link P0 使用二进制帧：`LUNA` 魔数、版本、消息类型、标志、请求 ID、载荷长度、最大 512 字节载荷和 CRC32。当前消息只有 `HELLO/HELLO_ACK`、`PING/PONG` 和 `TOUCH_TEST`。解析器支持拆包、粘包、CRC 错误丢弃及重新同步。

实现依据为 Espressif 的 [USB CDC 示例](https://github.com/espressif/esp-idf/blob/master/examples/peripherals/usb/device/tusb_serial_device/README.md) 和 [ESP32-P4 USB Device Stack 文档](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32p4/api-reference/peripherals/usb_device.html)。
