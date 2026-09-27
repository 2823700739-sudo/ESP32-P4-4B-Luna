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

本地配置和配对令牌保存在 `config.local.json` 中，该文件不会提交到 Git。
默认服务端口是 `8765`。首次运行时，如果 Windows 防火墙弹出提示，请允许
Python 访问专用网络。

启动日志中出现以下内容代表真实媒体状态同步已经启用：

```text
Windows media session synchronization is active.
Codex App Server synchronization is active.
```

启动时还会打印实际选中的 `Codex executable` 路径。助手会先检查 PATH，再自动
搜索 `%LOCALAPPDATA%\OpenAI\Codex\bin` 下带版本号的桌面端目录，因此新开的
PowerShell 没有 `codex` 命令也能正常工作。如果仍显示
`Codex App Server is unavailable`，请确认 Codex 桌面端已经登录。助手会每
15 秒自动重试，不可用期间面板明确显示待检查状态，不沿用旧额度冒充实时数据。

## 配置天气地点

设备不包含 GPS，也不会根据 IP 猜测位置。用自己的地点名称和坐标执行：

```powershell
.\pc-agent\set-weather.ps1 -Location "地点名称" -Latitude <纬度> -Longitude <经度>
```

经纬度使用十进制度数，例如东经和北纬为正数。保存后重启 Windows 助手。
天气卡显示天气状况、当前温度、体感温度、湿度、风速、最高和最低温、降水
概率、经纬度与更新时间。

## 接口

- `GET /health`：不需要鉴权的进程健康检查；
- `GET /api/v1/state`：读取电脑、音乐和其他卡片状态；
- `GET /api/v1/music/cover`：读取当前歌曲的 JPEG 封面；
- `POST /api/v1/actions`：发送白名单内的控制命令。

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
