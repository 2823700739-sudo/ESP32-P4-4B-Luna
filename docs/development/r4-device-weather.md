# R4 面板独立联网天气

天气卡的数据来源现改为 Luna 自身的 Wi-Fi，而不是 Windows Agent 的天气结果。面板第一次连接 Agent 时只读取地点名称与经纬度，并把位置写入 NVS；以后 Agent 关闭或 OTG 拔出，Luna 仍按 20 分钟间隔直接向 Open-Meteo 请求天气。位置在 Windows 端可继续用 `pc-agent/set-weather.ps1` 修改，下一次状态快照会同步到面板。

面板使用 HTTPS 和 ESP-IDF 根证书包验证服务端；时钟尚未通过 NTP 校准时不会贸然发起天气请求。成功结果写入 NVS；Wi-Fi 断开或请求失败时保留最后一次结果，显示 `CACHED`、来源和观测时间。地点尚未同步时，卡片提示先在 Windows 配置地点。

## 实机检查

| 检查项 | 通过标准 | 结果 |
| --- | --- | --- |
| 首次位置同步 | Agent 在线后，串口出现 `Device weather location updated from PC settings` | 待实测 |
| 面板直连天气 | 取得 Wi-Fi IP 和 NTP 时间后，串口出现 `Device weather refreshed from Open-Meteo` | 待实测 |
| 天气卡内容 | 地点、温度、体感、湿度、风速、最高/最低温、降水概率及更新时间正确 | 待目视确认 |
| Windows Agent 停止 | 天气卡不因 PC 离线被清空，20 分钟后仍能自行更新 | 待实测 |
| Wi-Fi 断开 | 显示最后天气和 `CACHED`，触摸、USB 状态与音乐不被阻塞 | 待实测 |
| Wi-Fi 恢复 | 自动发起新请求，成功后 `CACHED` 消失 | 待实测 |
| 断电重启 | 没有 Agent 时从 NVS 恢复地点和旧天气，联网后刷新 | 待实测 |

注意：本轮以 Agent 的位置配置作为首次配对入口；全新设备在首次取得位置前无法查询天气。HTTP/TLS 请求在独立 FreeRTOS 任务中执行，不应阻塞 LVGL 或 USB 动作。不能把编译通过当作上述实机验收通过。
