# Luna Windows 助手

Windows 助手负责向面板提供电脑状态，并接收经过白名单限制的控制命令。
音乐功能使用 Windows 全局系统媒体传输控制（GSMTC）读取网易云音乐的真实
播放状态，播放按钮不再根据点击次数自行猜测状态。

Codex 功能使用当前电脑已经登录的 Codex App Server，只读获取 5 小时窗口、
7 天窗口和最近工作区。助手不保存 ChatGPT 密码，也不要求额外填写 API Key。

天气功能按本地配置的地点与经纬度读取 Open-Meteo，不需要 API Key。更新在
后台执行，默认周期为 20 分钟，不会阻塞面板状态接口。

## 首次准备运行环境

在项目根目录执行：

```powershell
.\pc-agent\setup-agent.ps1
```

脚本会在 `pc-agent/.venv` 中创建独立 Python 环境并安装 WinRT 媒体会话依赖，
不会修改项目外的 Python 包。该目录已被 Git 忽略。

## 启动

```powershell
.\pc-agent\start-agent.ps1
```

首次安装环境和配置后，可在项目根目录执行一次：

```powershell
.\pc-agent\manage-autostart.ps1 -Mode install
```

这会为当前 Windows 用户创建登录时启动的计划任务。登录后 Agent 在后台等待；
只需插入 Luna 的 OTG 线，Agent 就会自动识别设备并建立连接。任务使用当前用户的
交互会话，以便读取网易云媒体状态和当前用户的 Codex 登录态，不在登录前作为系统
服务运行。重启电脑后的自动启动仍需在本机实际重新登录验证。

检查任务、临时启动或停止、取消自启动：

```powershell
.\pc-agent\manage-autostart.ps1 -Mode status
.\pc-agent\manage-autostart.ps1 -Mode start
.\pc-agent\manage-autostart.ps1 -Mode stop
.\pc-agent\manage-autostart.ps1 -Mode remove
```

后台运行日志保存在被 Git 忽略的 `pc-agent/agent.log`。`stop` 只停止计划任务
启动的实例；手动运行的 Agent 仍需在其窗口按 `Ctrl+C`。同时启动两个 Agent 时，
单实例保护会让第二个直接退出。仓库移动到其他路径后，应重新运行 `install` 更新
计划任务。`setup-agent.ps1` 仅首次安装或依赖变化时需要运行。

本地配置和配对令牌保存在 `config.local.json` 中，该文件不会提交到 Git。
默认服务端口是 `8765`。首次运行时，如果 Windows 防火墙弹出提示，请允许
Python 访问专用网络。

助手启动后还会自动发现 Luna 的原生 USB CDC 端口，不依赖固定 COM 号。正式助手与
`luna_usb_probe.py` 不能同时占用同一端口；启动前请先在探针窗口按 `Ctrl+C`。

启动日志中出现以下内容代表真实媒体状态同步已经启用：

```text
Windows media session synchronization is active.
Codex App Server synchronization is starting in the background.
Luna USB agent connected on COM28: firmware=R5-USB
Luna USB state snapshot served: sequence=... count=1
```

启动时还会打印实际选中的 `Codex executable` 路径。助手会先检查 PATH，再自动
搜索 `%LOCALAPPDATA%\OpenAI\Codex\bin` 下带版本号的桌面端目录，因此新开的
PowerShell 没有 `codex` 命令也能正常工作。如果仍显示
Codex 不可用，请确认 Codex 桌面端已经登录。助手在后台读取 Codex，默认每次刷新
完成后等待 15 秒再重试；状态和动作回复只读取缓存，不等待 App Server。首次读取、
失败或缓存超过刷新周期时明确显示不可用，不沿用旧额度冒充实时数据。

## 配置天气地点

设备不包含 GPS，也不会根据 IP 猜测位置。用自己的地点名称和坐标执行：

```powershell
.\pc-agent\set-weather.ps1 -Location "地点名称" -Latitude <纬度> -Longitude <经度>
```

经纬度使用十进制度数，例如东经和北纬为正数。保存后重启 Windows 助手。
天气卡显示天气状况、当前温度、体感温度、湿度、风速、最高和最低温、降水
概率、经纬度与更新时间。

## 接口

USB 握手成功后，状态快照、白名单动作和音乐封面优先通过 Luna Link 传输。
封面分块读取并验证长度与 SHA-256；未完成或校验失败的图片不会替换旧图。
USB 中断时，迁移期固件可在已取得 Wi-Fi IP 后回退以下 HTTP 接口。

- `GET /health`：不需要鉴权的进程健康检查；
- `GET /api/v1/state`：读取电脑、音乐和其他卡片状态；
- `GET /api/v1/diagnostics`：读取 Agent 侧 USB 连接与请求计数；
- `GET /api/v1/music/cover`：读取当前歌曲的 JPEG 封面；
- `POST /api/v1/actions`：发送白名单内的控制命令。

除 `/health` 外均需 `X-Luna-Token`。诊断接口里的 `connected` 表示当前已完成
Luna Link 握手；`connections` 和 `disconnects` 是本次 Agent 进程的连接会话数，
不是物理拔插次数。`errors_total` 包括找不到设备时的重试失败。
握手后连续 120 秒没有收到有效 Luna Link 帧，Agent 会关闭该会话并重新握手；
这个阈值高于设备端可配置的最长 60 秒状态轮询间隔，
以免串口仍开着但面板通信已停滞时继续显示“已连接”；
`last_frame_age_seconds` 可用于观察最近通信距今多久。
`state_requests` 等计数只在成功发送对应回复后增加。Agent 重启会清零这些计数。

新固件还通过 USB 发布设备自身诊断快照：`usb.device` 包含启动 ID、运行秒数、
重启原因、固件 ELF 哈希、内部 RAM/PSRAM 和固件 USB 计数。
`usb.device_stale` 标明是否为断线、旧会话或超过 120 秒未更新的缓存。
详见 [设备运行状态同步](../docs/development/r5-device-diagnostics.md)。

无需占用 COM 端口即可记录长时间链路状态：

```powershell
.\pc-agent\monitor-link.ps1 -DurationSeconds 28800 -IntervalSeconds 10
```

若要同时验收设备重启和内存记录，在上述命令后加 `-RequireDeviceDiagnostics`。
设备刚连接时应先等待首次诊断快照到达；旧固件可继续使用原来的不带参数命令。

脚本只查询本机 Agent，将采样写入被 Git 忽略的 `pc-agent/usb-soak-*.csv`；
如有断线、错误计数增加、Agent 重启或查询失败，会在结束时报告失败。它记录 Agent
观察到的断线；采样间隔内完全未被 Agent 观察到的异常仍需结合设备
Diagnostics 页和实际操作判断。

音乐命令包括 `music.previous`、`music.play`、`music.pause`、
`music.play_pause`、`music.next`、`music.volume_down`、`music.volume_up`、
`music.mute` 和 `music.volume_set`。`music.volume_set` 需要传入 `value` 整数，
范围为 0–100；状态接口的 `volume` 字段包含 `available`、`percent`、`muted`。
音量控制对象是 Windows 当前默认输出设备的系统主音量，静音状态会同步回面板。

助手优先选择 `cloudmusic.exe` 媒体会话。触屏播放键根据面板最近一次真实状态
发送明确的 `music.play` 或 `music.pause`，收到操作结果后再次校正图标。
`music.play_pause` 保留兼容旧版调用。上一首和
下一首也优先通过当前网易云媒体会话执行；媒体会话暂时不可用时才回退到
Windows 系统媒体键。明确播放和明确暂停在媒体会话不可用时直接返回失败，
避免用切换键产生相反结果。

助手会直接保留网易云返回的 UTF-8 曲名和歌手名，并为当前封面生成短哈希。
面板仅在哈希变化时重新下载封面，避免每次状态轮询都传输图片。封面接口与
状态、控制接口使用同一个 `X-Luna-Token` 鉴权头，最大接受 256 KiB 的 JPEG。

Codex 数据使用一个常驻的本地 `codex app-server` 子进程。首次请求完成
`initialize` 握手，之后读取 `account/rateLimits/read` 和按更新时间排序的
`thread/list`。项目名称取最近任务工作目录的末级目录名，避免把对话标题中的
临时文字当作项目名。读取结果缓存 15 秒，降低面板轮询对本地服务的压力。

天气数据每 20 分钟刷新并保存到被 Git 忽略的 `weather-cache.json`。外网请求
失败或电脑随后离线时，面板保留最后一次成功数据，并以黄色 `CACHED` 标记，
避免把旧天气当成实时数据。卡片保留 `Weather data: Open-Meteo` 数据来源标注。
