# 无语音版本实机验证

当前固件不加载语音模型，不监听唤醒词，也不识别或执行语音命令。触摸卡牌、
网易云音乐控制、Codex 状态、天气和诊断页仍可使用。诊断页的麦克风电平条只
验证硬件采集，不触发任何操作。

## 编译与烧录

在项目根目录运行：

```powershell
.\scripts\build-firmware.ps1 -Action reconfigure
.\scripts\build-firmware.ps1 -Action build
.\scripts\build-firmware.ps1 -Action flash -Port COM27
.\scripts\build-firmware.ps1 -Action monitor -Port COM27
```

此次无需烧录语音模型。串口监视器使用 `Ctrl+]` 退出。

当前完整镜像位于 `firmware/luna-panel/build/luna_panel_full.bin`；它会随之后的
卡牌开发继续更新，当前构建信息以[壁纸时钟卡实机验证](p1-clock-card.md)为准。
旧语音模型所在的闪存区不再属于有效分区，也不会被固件读取；为保持已有
`storage` 分区地址不变，该区域暂未复用。

## 实机检查

1. 开机后进入卡牌主页，确认不再显示 `Voice starting` 或唤醒提示；串口不应
   出现 `luna_voice`、`Wake word detected` 或 MultiNet 初始化日志。
2. 左右滑动 Codex、音乐、天气、时钟卡片，确认切换和数据更新正常。
3. 在音乐卡片触摸播放／暂停、上一首、下一首，确认 Windows 助手执行并回传
   实际播放状态。
4. 打开硬件诊断页，对麦克风说话，确认 `Mic` 电平条仍有变化；对麦克风说旧
   命令不应触发音乐或卡片操作。
5. 连续运行至少 30 分钟，观察有无复位、看门狗或 Wi-Fi 断连。出现异常时，
   保存从启动开始的串口日志及异常发生时正在使用的卡片。

以上实机项目需要在开发板上确认；仅编译成功不能代表实机验证通过。
