# Luna：烧录后开始使用

适用硬件：Waveshare **ESP32-P4-WIFI6-Touch-LCD-4B**，P4 rev1.x / 32 MB，板载 C6 已有兼容固件。

手机设置候选 **2026.11.26-phone-http**：烧录后保持供电 → 手机直接连接屏幕显示的 Luna 无密码热点 → 浏览器打开 **http://192.168.4.1** → 点击“扫描 Wi-Fi”、选家中网络并输入密码 → 连接、选城市并保存。时钟和天气不需要电脑脚本，完整说明见 [手机设置](PHONE-SETUP.md)。旧公开版没有此入口，实际验收状态见候选 manifest。

## 需要电脑相关功能时

电脑需要 **Windows 11 x64、蓝牙和 Python 3.11 x64**。仅音乐/系统音量、电脑指标、工程与 Codex 额度需以下 Agent：

1. **烧录固件。** 首次安装将 `luna-first-install-0x0.bin` 烧录到 **0x0**，完成后重启并保持 DC 供电。该文件会清除原有配对与天气缓存；已有 Luna 保留数据升级请使用 [完整教程](https://github.com/2823700739-sudo/ESP32-P4-4B-Luna/blob/codex/phone-wifi-setup/docs/DEPLOYMENT.md) 的 app-only 方法。不需要刷 C6。
2. **配对蓝牙。** 在 Windows“设置 → 蓝牙和设备 → 添加设备”中选择 **Luna**，核对电脑与屏幕的数字一致后分别确认。“已配对”但暂时“未连接”时继续下一步。
3. **双击启动。** 将随附的 Windows 使用包完整解压到一个固定文件夹，安装 [Python 3.11 x64](https://www.python.org/downloads/windows/)（保留 Python Launcher），双击根目录 **Start-Luna.cmd**。首次会联网安装依赖，随后启动 Agent，并设置当前用户登录后自动运行；不需要管理员权限。以后不要移动这个文件夹，也不要重复运行其他位置的副本。

连接后，设备时钟会同步，电脑卡出现 CPU / 内存数据。音乐需播放器支持 Windows 系统媒体会话；音量控制的是系统音量。

天气优先在手机上设置；`Configure-Weather.cmd` 仅保留可选 BLE 兼容入口，见 [BLE 设置](WEATHER-SETUP.md)，正常启动不再询问天气。Codex 额度需要电脑已有可用且已登录的 Codex 环境，不影响独立天气/时钟。当前时区为北京时间。

## 常见问题

- 提示找不到 Python：安装 **3.11 x64** 并保留 Python Launcher，然后重试。脚本不会替你安装系统 Python。
- 提示依赖下载失败：检查联网和错误提示后重试；无需反复删除蓝牙配对。
- 提示另一个文件夹已安装 Luna：使用原文件夹中的启动脚本。新副本不会覆盖或停止原常驻；需要迁移时参照完整教程手动卸载旧任务。
- 任务 Running 但没有数据：保持设备供电、电脑蓝牙打开并完成配对，等待自动连接。查看状态：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File pc-agent/manage-ble-resident.ps1 -Action Status
```

`LinkState=Connected` 才表示实际已连接。停止或卸载当前用户的登录启动任务，分别将上述 `Status` 改为 `Stop` 或 `Remove`；卸载保留源码和设备配对。

维护者可运行 `powershell -NoProfile -ExecutionPolicy Bypass -File pc-agent/setup-luna.ps1 -PrepareOnly` 只准备依赖：这个模式不会注册任务、连接蓝牙、打开串口或执行媒体动作。

若 Python 3.11 未注册到 Python Launcher，可在上述命令追加 `-PythonPath "C:\Python311\python.exe"`，明确指定自己的 x64 Python 3.11；示例路径需替换为实际安装位置，不会自动安装 Python。

源代码与许可：[GitHub](https://github.com/2823700739-sudo/ESP32-P4-4B-Luna) · [Apache-2.0](../LICENSE) · [第三方说明](../THIRD_PARTY_NOTICES.md)。分发使用包或固件时保留完整许可文件。
