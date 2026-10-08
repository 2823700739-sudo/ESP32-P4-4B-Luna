# Luna 部署教程

**中文** · [English](DEPLOYMENT.en.md) · [项目介绍](../README.md)

本教程对应 **2026.10.08-preview**。源代码与公开 bin 都在仓库中。公开镜像没有 Wi-Fi 名称、密码或设备 NVS 数据；时钟、音乐、额度和电脑状态通过 BLE 使用。联网天气需自行配置构建，且新设备的地点设置入口尚未提供。


许可材料分发版为 `2026.10.08-preview.1`，bin 内置版本仍为 `2026.10.08-preview`。推荐下载[完整源码、bin 与许可 ZIP](https://github.com/2823700739-sudo/ESP32-P4-4B-Luna/archive/refs/tags/v2026.10.08-preview.1.zip)；单独分发 bin 时同时附带 [LICENSE](../LICENSE)、[NOTICE](../NOTICE)、[第三方说明](../THIRD_PARTY_NOTICES.md)与完整 [LICENSES/](../LICENSES/) 目录。

## 1. 准备与适用范围

- Waveshare ESP32-P4-WIFI6-Touch-LCD-4B，720×720，P4 rev1.x，32 MB Flash；其他板型、rev3.x 和屏幕型号不要直接刷这个镜像。
- 板载 C6 需要兼容当前 ESP-Hosted / HCI 通路；本包不含 C6 固件，不更新 C6。
- Windows 11（build ≥ 22000）、可用蓝牙、Python 3.10，数据线连接板上的下载/串口接口。刷写端口以本机实际识别的 COM 为准。
- 首次安装会写入 P4 bootloader 和分区表。若设备有其他产品或重要数据，先确认布局与备份；常规 Luna 升级仅写 app。不要执行整片擦除或使用 `--force` 跳过芯片兼容检查。

先下载完整仓库 ZIP 并解压，或：

```powershell
git clone --branch codex/usb-r2 https://github.com/2823700739-sudo/ESP32-P4-4B-Luna.git
cd ESP32-P4-4B-Luna
```

以下命令从仓库根目录执行。GitHub 页面单独下载 bin 时，请使用文件页的 **Download raw file**，不要把网页保存成 bin。

## 2. 文件与校验

公开文件在 [固件目录](../firmware/releases/2026.10.08-preview/README.md)：

| 文件 | 作用 | 写入地址 |
| --- | --- | --- |
| `luna-panel.bin` | 五卡 B1 app；已有 Luna 的正常升级使用它 | `0x20000` |
| `bootloader.bin` | P4 启动程序；首次安装使用 | `0x2000` |
| `partition-table.bin` | 10 MiB app 分区布局；首次安装使用 | `0x10000` |
| `manifest.json` / `SHA256SUMS.txt` | 版本、硬件、地址、大小与 SHA-256 | 不刷写 |

```powershell
Get-Content firmware/releases/2026.10.08-preview/SHA256SUMS.txt
Get-FileHash firmware/releases/2026.10.08-preview/*.bin -Algorithm SHA256
```

三份 bin 的哈希均应与校验文件一致。只下载 bin 与清单，不下载或刷写维护者的 NVS、配对、TF、私人配置和全片 Flash 备份。

## 3. 安装独立刷写工具

无需为了刷公开 bin 安装完整 ESP-IDF。使用独立虚拟环境：

```powershell
py -3.10 -m venv .venv-flash
.\.venv-flash\Scripts\python.exe -m pip install esptool==5.4.0
```

如果已安装 BLE 常驻，刷写前先停止它：

```powershell
.\pc-agent\manage-ble-resident.ps1 -Action Stop
```

使用本机实际端口替换下方 `COMx`。这不是可直接使用的端口名。若无法进入下载模式，按照板厂说明使用 BOOT / RESET；不要连接或刷写 C6 的下载口。

## 4. 已有 Luna：只升级 app

仅适用于已确认 app 起始地址 `0x20000`、容量 10 MiB，storage 起始地址 `0xA20000` 的 B1 布局；不适用于旧 6 MiB app 或其他产品分区。

```powershell
.\.venv-flash\Scripts\python.exe -m esptool --chip esp32p4 --port COMx write-flash `
  --flash-mode keep --flash-freq keep --flash-size keep `
  0x20000 firmware/releases/2026.10.08-preview/luna-panel.bin
```

该命令只写 app，不写分区表、NVS 或 TF，也不升级 C6。**公开 app 的 Wi-Fi 凭据为空**，所以已有联网天气需要使用自己配置的本地构建 app；公开 bin 不能读取并继承旧 app 中编译的 Wi-Fi 密码。

## 5. 首次安装到匹配硬件

选择首次安装前确认板型、芯片版本、32 MB Flash 与上述布局；存在其他固件/数据时不要直接当作空白设备处理。当前公开镜像尚未完成全新设备安装验收。

```powershell
.\.venv-flash\Scripts\python.exe -m esptool --chip esp32p4 --port COMx write-flash `
  --flash-mode dio --flash-freq 80m --flash-size 32MB `
  0x2000 firmware/releases/2026.10.08-preview/bootloader.bin `
  0x10000 firmware/releases/2026.10.08-preview/partition-table.bin `
  0x20000 firmware/releases/2026.10.08-preview/luna-panel.bin
```

地址和 Flash 参数来自本版构建清单。没有 `erase-flash`、NVS 文件、全片合并镜像或 C6 镜像。刷写成功后重启 P4，保留 DC 供电，设备应进入 Luna 界面与蓝牙初始化。如果报告芯片不兼容、NVS 初始化失败或 C6 controller unavailable，停止并查明原因，不绕过检查或擦除恢复。

## 6. 配对并安装 Windows Agent

安装独立 Agent 环境：

```powershell
py -3.10 -m venv pc-agent/.venv-ble
.\pc-agent\.venv-ble\Scripts\python.exe -m pip install -r pc-agent/requirements-ble-music.txt
```

1. 在 Windows 蓝牙设置添加 **Luna**。
2. 核对 Windows 与 Luna 屏幕上的数字完全相同，分别确认。数字不匹配就拒绝。
3. 安装当前用户登录常驻并启动：

```powershell
.\pc-agent\manage-ble-resident.ps1 -Action Install
.\pc-agent\manage-ble-resident.ps1 -Action Start
.\pc-agent\manage-ble-resident.ps1 -Action Status
```

设备首次获得有效时间后显示时钟，电脑指标随后台采集更新。播放器须支持 Windows GSMTC。Codex 额度需本机已有可用的 Codex CLI 与登录状态；缺少时额度显示未知，其余功能可用。将目标 VS Code 工程切到前台一次以识别工程名。

`State=Running` 表示计划任务运行；查看 `LinkState=Connected` 与近期日志才能判断连接：

```powershell
Get-Content pc-agent/ble-link.log -Tail 12
```

常驻在当前交互用户下运行，不需要管理员权限或保存密码；自动登录启动，异常退出后重试。不要同时启动第二个 BLE 客户端。停止或卸载：

```powershell
.\pc-agent\manage-ble-resident.ps1 -Action Stop
.\pc-agent\manage-ble-resident.ps1 -Action Remove
```

## 7. 从源代码构建

安装 ESP-IDF **6.0.2** 及 ESP32-P4 工具链，设置 `IDF_PATH`；工具不在默认每用户 `.espressif` 时另设 `IDF_TOOLS_PATH`。包装脚本也接受 `-IdfPath` / `-IdfToolsPath`。

### 构建可公开的无凭据版本

```powershell
.\scripts\build-firmware.ps1 -PublicRelease -Action reconfigure
.\scripts\build-firmware.ps1 -PublicRelease -Action build
```

该模式使用独立 `sdkconfig.public_b1` 与 `build-public-b1`，拒绝非空 Wi-Fi 凭据和刷写/监视动作；固定 360 MHz 与当前双缓冲显示方案。app 输出为 `firmware/luna-panel/build-public-b1/luna_panel.bin`，bootloader 和 partition-table 位于该构建目录对应子目录。公开前仍需核对文件、来源与凭据。

### 构建私人联网版本

```powershell
.\scripts\build-firmware.ps1 -BleB1 -Action reconfigure
```

在已初始化的 ESP-IDF 终端进入 `firmware/luna-panel`，配置 Wi-Fi：

```powershell
idf.py -B build-ble-b1 -D SDKCONFIG=sdkconfig.ble_b1 menuconfig
```

在 **Luna Current Product** 填写 2.4 GHz SSID / 密码；返回仓库根目录：

```powershell
.\scripts\build-firmware.ps1 -BleB1 -Action reconfigure
.\scripts\build-firmware.ps1 -BleB1 -Action build
```

本地 app 在 `firmware/luna-panel/build-ble-b1/luna_panel.bin`。该镜像会包含输入的 Wi-Fi 凭据，只用于自己的设备，不上传 GitHub。用第 4 节 app-only 命令替换 app 文件路径即可升级匹配布局。

已有 NVS 天气地点可保留，**新设备目前没有地点设置入口**；填写 Wi-Fi 本身不能补齐地点。时区固定北京时间，暂不支持设备端切换。

## 8. 常见问题

| 现象 | 处理 |
| --- | --- |
| 找不到 COM | 核对数据线、板载下载接口与 Windows 设备管理器；端口按本机实际识别，不使用历史编号 |
| 芯片/分区不匹配 | 停止刷写，确认 P4 rev1.x 与 B1 10 MiB 布局，不用 force 或整片擦除 |
| 任务 Running，卡片仍离线 | 查看 LinkState 和近期 ble-link.log；检查设备供电/广播、Windows 蓝牙与配对，避免并行客户端 |
| 配对数字或旧绑定问题 | 拒绝不匹配数字；需要恢复时只按屏幕明确的绑定恢复操作处理，不清空整个 NVS |
| 天气“尚未设置地点” | 空白设备的配置入口尚未提供；不是修改 Agent 或安装服务能解决的配置项 |
| 公共 bin 没有联网 | Wi-Fi 凭据为空是发布设计；需要私人配置构建，不上传该私人镜像 |
| 音乐、额度或工程未知 | 检查 GSMTC 播放器、本地 Codex 状态、默认 VS Code 窗口标题；未知不等于零 |

组件与素材许可见[项目介绍](../README.md#当前限制与许可)，开发架构见[开发文档](DEVELOPMENT.md)。
