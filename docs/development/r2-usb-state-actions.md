# R2 USB 状态与动作验证

本阶段把电脑状态、白名单动作和音乐封面迁移到 Luna Link。Windows Agent 同时保留 HTTP 服务；固件在 USB 握手成功时优先使用 USB，传输中断时可临时回退 HTTP。

HTTP 回退只有在设备取得 Wi-Fi IP 后才会启用。这样 USB 同步任务可以在启动早期运行，但在 `esp_netif/lwIP` 尚未就绪时不会误入 DNS/HTTP 路径；Wi-Fi 断开时也会立即关闭 HTTP 回退。

## 运行前

只能有一个程序占用 `COM28`。如果此前运行着 `luna_usb_probe.py`，先在该窗口按 `Ctrl+C`。探针和正式 Agent 不能同时打开同一 CDC 串口。

更新依赖并启动正式 Agent：

```powershell
.\pc-agent\setup-agent.ps1
.\pc-agent\start-agent.ps1
```

启动成功后应看到：

```text
Luna USB transport is enabled.
Luna USB agent connected on COM28: firmware=R2-USB
Luna USB state snapshot served: sequence=... count=1
```

## 当前 USB 消息

- `STATE_REQUEST / STATE_SNAPSHOT`：Codex、最近项目、音乐元数据、系统音量和当前天气状态；
- `ACTION_REQUEST / ACTION_RESULT`：播放、暂停、上一首、下一首、设置音量和静音；
- `COVER_INFO_REQUEST / COVER_INFO`：以状态快照中的 16 字符 `cover_id` 查询 JPEG 总长度和 SHA-256；
- `COVER_CHUNK_REQUEST / COVER_CHUNK`：按偏移读取最多 2048 字节；每块核对长度和偏移，完整图片再核对 SHA-256；
- 动作继续经过 Windows Agent 的白名单和 `request_id` 去重；
- 单帧载荷上限为 4096 字节，帧仍包含版本、请求 ID、长度和 CRC32；
- USB 传输超时或断线时保留现有 HTTP 请求；图片缺失、长度错误或校验失败时丢弃临时图片，等待下一次状态轮询。

## 实机检查

| 检查项 | 通过标准 | 实测结果 |
| --- | --- | --- |
| 启动稳定性 | Agent 未连接时不触发 `Invalid mbox`，设备不循环复位 | 实机通过：单次 `POWERON` 后进入主界面并取得 IP |
| USB 状态快照 | Agent 记录 `state snapshot served` | 实机通过：COM28 连接为 `R2-USB`，成功返回快照 |
| Codex/项目卡 | USB 工作且 Wi-Fi HTTP 停止时仍能刷新 | 待实测 |
| 音乐状态 | 标题、歌手、播放状态通过 USB 更新 | 待实测 |
| 音量状态 | Windows 音量变化能反映到面板 | 待实测 |
| 白名单动作 | 面板播放、切歌、音量操作收到 USB 结果 | 待实测 |
| 动作去重 | 相同 `request_id` 不重复执行 | 单元测试通过，待实机故障注入 |
| USB 中断回退 | 断开 OTG 后现有 HTTP 仍可工作 | 待实测 |
| 音乐封面 | Wi-Fi 不可用时 USB 分块更新、切歌后图片与曲目匹配 | 2026-09-29：固件在取得 IP 前记录 `Music cover received over USB (2878 bytes)`；屏幕显示和切歌仍待目视验收 |
| 封面异常 | 传输中断、旧 `cover_id` 或校验失败不替换旧图 | 单元测试覆盖旧 ID；待实机故障注入 |

本地编译和单元测试不能代替面板显示、Windows 媒体控制及断线恢复的实机验证。

本轮实机运行固件版本为 `642b8d2`。完整镜像在 COM27 写入并通过哈希校验；COM28
由正式 Agent 握手后提供封面块、状态快照和动作结果。启动日志只出现一次
`POWERON`，封面 USB 接收成功时 Wi-Fi 尚未取得 IP。调试监视器已经退出，COM27 可继续用于烧录。
