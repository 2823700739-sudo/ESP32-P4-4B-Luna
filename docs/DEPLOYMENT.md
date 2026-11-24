# Luna 部署教程

**中文** · [English](DEPLOYMENT.en.md) · [项目介绍](../README.md)

本教程对应 **2026.10.08-preview**。源代码与公开 bin 都在仓库中。公开镜像没有 Wi-Fi 名称、密码或设备 NVS 数据；时钟、音乐、额度和电脑状态通过 BLE 使用。联网天气需自行配置构建，且新设备的地点设置入口尚未提供。

合并固件分发版为 `2026.11.24-preview`，bin 内置版本仍为 `2026.10.08-preview`。推荐下载[完整源码、bin 与许可 ZIP](https://github.com/2823700739-sudo/ESP32-P4-4B-Luna/archive/refs/tags/v2026.11.24-preview.zip)；单独分发 bin 时同时附带 [LICENSE](../LICENSE)、[NOTICE](../NOTICE)、[第三方说明](../THIRD_PARTY_NOTICES.md)与完整 [LICENSES/](../LICENSES/) 目录。

## 1. 准备与适用范围

- Waveshare ESP32-P4-WIFI6-Touch-LCD-4B，720×720，P4 rev1.x，32 MB Flash；其他板型、rev3.x 和屏幕型号不要直接刷这个镜像。
- 板载 C6 需要兼容当前 ESP-Hosted / HCI 通路；本包不含 C6 固件，不更新 C6。
- Windows 11 x64（build ≥ 22000）、可用蓝牙、64 位 x64 Python 3.11，数据线连接板上的下载/串口接口。刷写端口以本机实际识别的 COM 为准。
- 首次安装会写入 P4 bootloader 和分区表。若设备有其他产品或重要数据，先确认布局与备份；常规 Luna 升级仅写 app。不要执行整片擦除或使用 `--force` 跳过芯片兼容检查。

Python 可从 [Python 官方 Windows 下载页](https://www.python.org/downloads/windows/)安装；选择 3.11 的 Windows installer (64-bit)，并启用 Python Launcher。本文的新装命令使用 `py -3.11`；已有 3.10 常驻不必为本次固件打包重建。

先下载本版完整仓库 ZIP 并解压，或：

```powershell
git clone --depth 1 --branch v2026.11.24-preview https://github.com/2823700739-sudo/ESP32-P4-4B-Luna.git
cd ESP32-P4-4B-Luna
```

以下命令从仓库根目录执行。GitHub 页面单独下载 bin 时，请使用文件页的 **Download raw file**，不要把网页保存成 bin。

## 2. 文件与校验

公开文件在 [固件目录](../firmware/releases/2026.10.08-preview/README.md)：

| 文件 | 作用 | 写入地址 |
| --- | --- | --- |
| `luna-first-install-0x0.bin` | 首次安装单文件；覆盖 NVS / 配对，不能用于保留数据的升级 | `0x0` |
| `luna-panel.bin` | 五卡 B1 app；已有 Luna 的正常升级使用它 | `0x20000` |
| `bootloader.bin` | P4 启动程序；首次安装使用 | `0x2000` |
| `partition-table.bin` | 10 MiB app 分区布局；首次安装使用 | `0x10000` |
| `manifest.json` / `SHA256SUMS.txt` | 版本、硬件、地址、大小与 SHA-256 | 不刷写 |

```powershell
Get-Content firmware/releases/2026.10.08-preview/SHA256SUMS.txt
Get-FileHash firmware/releases/2026.10.08-preview/*.bin -Algorithm SHA256
```

所下载 bin 的哈希均应与校验文件一致。只下载 bin 与清单，不下载或刷写维护者的 NVS、配对、TF、私人配置和全片 Flash 备份。

## 3. 安装独立刷写工具

无需为了刷公开 bin 安装完整 ESP-IDF。使用独立虚拟环境：

```powershell
py -3.11 -m venv .venv-flash
.\.venv-flash\Scripts\python.exe -m pip install esptool==5.4.0
```

如果已安装 BLE 常驻，刷写前先停止它：

```powershell
.\pc-agent\manage-ble-resident.ps1 -Action Stop
```

使用本机实际端口替换下方 `COMx`。这不是可直接使用的端口名。若无法进入下载模式，按照板厂说明使用 BOOT / RESET；不要连接或刷写 C6 的下载口。

如果 PowerShell 提示禁止运行 `.ps1`，可将对应管理命令写成 `powershell -NoProfile -ExecutionPolicy Bypass -File .\pc-agent\manage-ble-resident.ps1 -Action Stop`（其他 Action 同理）。它只影响该进程，不需要修改全局策略；组织策略禁止时联系管理员。

## 4. 已有 Luna：只升级 app

仅适用于已确认 app 起始地址 `0x20000`、容量 10 MiB，storage 起始地址 `0xA20000` 的 B1 布局；不适用于旧 6 MiB app 或其他产品分区。

```powershell
.\.venv-flash\Scripts\python.exe -m esptool --chip esp32p4 --port COMx write-flash `
  --flash-mode keep --flash-freq keep --flash-size keep `
  0x20000 firmware/releases/2026.10.08-preview/luna-panel.bin
```

该命令只写 app，不写分区表、NVS 或 TF，也不升级 C6。**公开 app 的 Wi-Fi 凭据为空**，所以已有联网天气需要使用自己配置的本地构建 app；公开 bin 不能读取并继承旧 app 中编译的 Wi-Fi 密码。

## 5. 首次安装：单个 bin 从 0x0 烧录

本节只适用于**可清空的首次安装设备**。先核对板型、P4 **rev1.x**、32 MB Flash、B1 分区，以及板载 C6 已有兼容当前 ESP-Hosted / HCI 的固件。合并包只包含 P4 bootloader、分区表和 app，**不能初始化空白 C6**；C6 不兼容时停止，不随机刷写其他 C6 镜像。全新设备安装尚待实机验收。

**合并 raw bin 的间隙填充为 `0xFF`，从 `0x0` 写入会清除 `0x11000–0x16FFF` 的 NVS，包括已有 BLE 配对、天气地点和缓存；也覆盖 phy_init。已有 Luna 要保留这些数据，请使用第 4 节 app-only，不能使用合并包。** 即使不执行 `erase-flash`，这些间隙仍会被擦写，详见 [esptool RAW 格式说明](https://docs.espressif.com/projects/esptool/en/latest/esp32p4/esptool/basic-commands.html#raw-output-format)。

先用实际端口查看芯片版本与 Flash 容量，**只有 P4 rev1.x / 32 MB 才继续**：

```powershell
.\.venv-flash\Scripts\python.exe -m esptool --chip esp32p4 --port COMx flash-id
```

核对 SHA-256 后，只选下面这一个文件、地址 `0x0`（`0x00` 相同）：

```powershell
.\.venv-flash\Scripts\python.exe -m esptool --chip esp32p4 --port COMx write-flash `
  --flash-mode keep --flash-freq keep --flash-size keep `
  0x0 firmware/releases/2026.10.08-preview/luna-first-install-0x0.bin
```

文件已内置正确的 DIO / 80 MHz / 32 MB 参数。不需要在本地合并或安装 ESP-IDF。若使用已有的兼容 P4 下载工具，同样只勾选该文件、填 `0x0`；不要勾选整片擦除、不要改为 app 地址 `0x20000`。合并 raw 文件开头是填充字节，不能依赖工具从首字节自动校验所有内嵌镜像的芯片版本，所以必须先核对上述硬件条件。

合并文件长 `9569312` bytes（`0x920420`），扇区擦写结束地址为 `0x921000`（不含），没有填满 32 MB。该写入范围不覆盖 `0xA20000` 开始的 storage，也不写 TF 或独立的 C6 Flash；这不表示 NVS 得到保留。

### 可选：分文件安装

也可以选择下面的三个文件与各自地址，**与合并包二选一，不要重复执行**。分文件命令不填充 NVS 间隙，但不能保证其他产品的旧 NVS 与 Luna 兼容；它不是任意旧固件的无损迁移工具。

```powershell
.\.venv-flash\Scripts\python.exe -m esptool --chip esp32p4 --port COMx write-flash `
  --flash-mode keep --flash-freq keep --flash-size keep `
  0x2000 firmware/releases/2026.10.08-preview/bootloader.bin `
  0x10000 firmware/releases/2026.10.08-preview/partition-table.bin `
  0x20000 firmware/releases/2026.10.08-preview/luna-panel.bin
```

烧录后松开 BOOT 并重启 P4，保持 DC 供电。应出现五卡界面、触摸切卡与蓝牙初始化；时钟需要下一节 Agent 校时，天气和额度不会因烧录而自动配置。如果报告芯片不兼容、NVS 初始化失败或 C6 controller unavailable，停止并查明原因，不用 force 或整片擦除绕过。

## 6. 配对并安装 Windows Agent

安装独立 Agent 环境：

```powershell
py -3.11 -m venv pc-agent/.venv-ble
.\pc-agent\.venv-ble\Scripts\python.exe -m pip install -r pc-agent/requirements-ble-music.txt
.\pc-agent\.venv-ble\Scripts\python.exe -m pip check
.\pc-agent\.venv-ble\Scripts\python.exe pc-agent/check-deployment.py
```

上述检查应显示 `Agent prerequisites OK`；失败时先修复版本/依赖，不要反复启动常驻。检查不会扫描蓝牙、配对或操控媒体。Windows 设置中需开启蓝牙；关闭蓝牙扫描权限/禁用适配器时检查通过也不能建立连接。

1. 在 Windows 蓝牙设置添加 **Luna**。
2. 核对 Windows 与 Luna 屏幕上的数字完全相同，分别确认。数字不匹配就拒绝。
3. 安装当前用户登录常驻并启动：

```powershell
.\pc-agent\manage-ble-resident.ps1 -Action Install
.\pc-agent\manage-ble-resident.ps1 -Action Start
.\pc-agent\manage-ble-resident.ps1 -Action Status
```

设备首次获得有效时间后显示时钟，电脑指标随后台采集更新。播放器须支持 Windows GSMTC。Codex 额度需本机已有可用的 Codex CLI 与登录状态；缺少时额度显示未知，其余功能可用。将目标 VS Code 工程切到前台一次以识别工程名。

安装完成的最小检查：Luna 能触摸切卡；配对确认后 `LinkState=Connected`；时钟同步、CPU/RAM 有读数。音乐需要支持 GSMTC 的播放器，额度依赖已有可用 Codex 环境；天气首次地点入口尚未提供，因此不能将所有五卡联网数据当作新设备默认可用。

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

### 复现公开合并包（维护者可选）

对仓库中已发布的三份无凭据 bin，可用独立刷写环境离线重现合并包；不要把私人构建 bin 覆盖进公开目录：

```powershell
.\.venv-flash\Scripts\python.exe scripts/merge-public-firmware.py
.\.venv-flash\Scripts\python.exe scripts/merge-public-firmware.py --check
```

脚本在合并前核对原三份文件的地址、大小、SHA 与 B1 分区，合并后核对各段原字节和填充区；不连接设备，不将文件填满 Flash。

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
| 合并包刷后需要重新配对 | 合并 raw bin 清除了 NVS；Windows 仍有旧绑定时移除该 Luna 的旧记录，再按屏幕数字重新确认。正常升级应使用 app-only |
| Python / 依赖检查失败 | 使用 Windows x64 与 64 位 Python 3.11，在同一 .venv-ble 中安装 requirements-ble-music.txt，先通过 pip check 和 check-deployment.py |
| 公共 bin 没有联网 | Wi-Fi 凭据为空是发布设计；需要私人配置构建，不上传该私人镜像 |
| 音乐、额度或工程未知 | 检查 GSMTC 播放器、本地 Codex 状态、默认 VS Code 窗口标题；未知不等于零 |

组件与素材许可见[项目介绍](../README.md#当前限制与许可)，开发架构见[开发文档](DEVELOPMENT.md)。
