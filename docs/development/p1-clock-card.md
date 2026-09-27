# 壁纸时钟卡实机验证

> 本页记录首次加入时钟卡的版本。当前滑动聚焦效果和最新构建信息见
> [卡牌聚焦效果实机验证](p1-focus-carousel.md)。

## 本轮内容

- 新增第四张 `CLOCK` 卡牌，使用内置渐变与月亮背景显示北京时间、日期和秒数。
- 时钟直接通过开发板 Wi-Fi 向 NTP 服务器校时，不依赖 Windows 助手。
- 四张卡牌循环滑动；界面同时只保留当前卡与左右邻卡的 LVGL 对象，远离视窗的
  卡牌对象在切换结束后销毁。音乐封面的压缩数据仍保留在内存中，回到音乐卡时
  可直接重新显示。
- 语音控制保持关闭。此阶段尚不支持上传自选壁纸图片。

## 烧录

在项目根目录运行：

```powershell
.\scripts\build-firmware.ps1 -Action reconfigure
.\scripts\build-firmware.ps1 -Action build
.\scripts\build-firmware.ps1 -Action flash -Port COM27
.\scripts\build-firmware.ps1 -Action monitor -Port COM27
```

串口监视器用 `Ctrl+]` 退出。默认 NTP 服务器为 `pool.ntp.org`，时区为
`CST-8`（北京时间）；可在 `menuconfig` 的 Luna 配置中更改。首次校时前卡片
应显示“等待网络校时”，不应显示错误日期。若路由器无法访问 NTP 服务器，
请记录串口日志，并在 `menuconfig` 中改用可访问的服务器。

## 实机检查

1. 从 `CODEX` 向左依次滑到 `MUSIC`、`WEATHER`、`CLOCK`，继续滑动应回到
   `CODEX`；反方向也应循环。卡牌指示应显示 `1 / 4` 到 `4 / 4`。
2. 在 `CLOCK` 卡等待校时。时间应按北京时间显示，秒数逐秒变化，日期应与
   当前日期一致。
3. 保持开发板供电，让 Windows 助手退出或电脑休眠。时钟应继续计时；若仅
   电脑休眠，需先确认电脑 USB 端口仍给板子供电。
4. 连续左右滑动至少 30 次，特别反复经过音乐卡与时钟卡，确认封面恢复、
   按钮可用，没有黑屏、残影、卡死或复位。
5. 冷启动时断开互联网但保留 Wi-Fi。若时间尚未校准，应保持“等待网络校时”；
   恢复互联网后观察是否完成校时。

## 本机检查结果

- ESP-IDF 6.0.2：重新配置和编译通过。
- 当时的完整镜像：`firmware/luna-panel/build/luna_panel_full.bin`，`1,945,104` 字节，
  SHA-256：`C0A0DF253E62BBAE002D5B8140B2D25C15609A791D50656708FCB14A25330D20`。
- 实板滑动、NTP 校时、PC 休眠：等待烧录验证。
