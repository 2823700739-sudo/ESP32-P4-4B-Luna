# P1 Codex 实时额度与当前项目实机验证

## 本轮目标

本轮将 Codex 综合卡从占位数据接入当前 Windows 登录态，验证以下内容：

- 左侧大号数字显示当前主额度窗口的剩余百分比；
- 同时显示主窗口和 7 天窗口的剩余量及重置时间；
- 右侧显示最近操作任务所在的项目目录名称与完整路径；
- Codex 本地服务异常时显示不可用状态，不把缓存旧值当成实时额度；
- 音乐控制、卡片滑动和原有硬件诊断继续正常工作。

Windows 助手通过本机 `codex app-server` 读取数据，沿用 Codex 桌面端当前登录
状态，不需要配置 API Key，也不会读取或保存 ChatGPT 密码。

## 第一步：重启 Windows 助手

在原助手窗口按 `Ctrl+C` 停止旧进程，然后执行：

```powershell
cd D:\Documents\ChatGPT\ESP32-P4-4B-Luna
.\pc-agent\start-agent.ps1
```

正常启动时应出现：

```text
Codex executable: C:\Users\...\AppData\Local\OpenAI\Codex\bin\...\codex.exe
Luna agent listening on http://0.0.0.0:8765
Windows media session synchronization is active.
Codex App Server synchronization is active.
```

助手会自动搜索 Codex 桌面端带版本号的安装目录，不依赖 PowerShell 的 PATH。
若仍显示 `Codex App Server is unavailable`，检查自动发现结果：

```powershell
Get-ChildItem "$env:LOCALAPPDATA\OpenAI\Codex\bin" -Filter codex.exe -File -Recurse
```

确认 Codex 桌面端保持登录后再重启助手。助手每 15 秒刷新一次额度，短时间内
多次请求 `/api/v1/state` 不会重复启动 Codex 进程。

## 第二步：烧录最新完整镜像

将开发板 USB 转串口连接到 `COM27`，另开 PowerShell 执行：

```powershell
cd D:\Documents\ChatGPT\ESP32-P4-4B-Luna
.\scripts\build-firmware.ps1 -Action flash-full -Port COM27
```

必须使用 `flash-full` 将完整镜像从 `0x0` 写入。烧录后可查看串口：

```powershell
.\scripts\build-firmware.ps1 -Action monitor -Port COM27
```

按 `Ctrl+]` 退出监视器。

## 第三步：验证实时额度

1. 等待顶栏 PC 状态变为就绪；
2. 滑到 `CODEX WORKSPACE` 卡；
3. 对照 Codex 桌面端用量页，检查左侧大号剩余百分比；
4. 检查左侧文字同时包含主窗口和 7 天窗口；
5. 检查下方显示两个窗口的本地重置时间；
6. 保持页面 20 秒以上，确认额度刷新时界面没有闪退或复位。

额度采用“剩余百分比”显示。例如服务返回已使用 84%，面板显示 16%。整数值
可能因两边刷新时刻不同出现 1% 的短暂差异，等待下一次轮询后应一致。

## 第四步：验证当前项目

1. 在 Codex 中打开或继续本项目任务；
2. 等待最多 20 秒；
3. 卡片右侧项目名称应显示 `ESP32-P4-4B-Luna`；
4. 下方路径应显示当前工作目录；
5. 切换到另一个 Codex 项目并产生一次新操作；
6. 等待刷新，确认名称和路径切换到最近项目。

项目顺序来自 Codex 最近任务的工作目录，而不是对话标题。同一目录下的多个
任务只算一个项目，当前接口最多保留三个最近且不重复的项目，面板本轮显示
其中第一个。

## 第五步：验证异常回退

1. 停止 Windows 助手，确认面板 PC 状态转为检查中；
2. 重新启动助手，确认 PC 与 Codex 信息恢复；
3. 如需单独验证 Codex 异常，可临时把 `config.local.json` 中
   `codex_executable` 设置为不存在的路径，然后重启助手；
4. 此时卡片应显示 `Codex unavailable` 和 `Check Windows agent`；
5. 删除该临时配置或恢复为空字符串并重启助手，额度应恢复。

不要修改配对令牌。异常验证完成后，确认 Windows 助手没有连续创建大量
`codex.exe` 进程。

## 实机验证记录

| 验证项目 | 预期结果 | 实际结果 | 备注 |
| --- | --- | --- | --- |
| COM27 完整烧录 | 无错误并自动复位 |  |  |
| Windows 助手启动 | 显示 Codex 同步已启用 |  |  |
| 主窗口剩余额度 | 与 Codex 桌面端一致 |  |  |
| 7 天剩余额度 | 与 Codex 桌面端一致 |  |  |
| 两个重置时间 | 显示本地日期和时间 |  |  |
| 当前项目名称 | 显示最近任务所在目录名 |  |  |
| 当前项目路径 | 显示最近任务完整路径 |  |  |
| 连续运行 30 分钟 | 无卡死、看门狗或复位 |  |  |
| 助手停止再启动 | PC 与 Codex 状态自动恢复 |  |  |
| Codex 服务不可用 | 明确显示不可用，不保留旧额度 |  |  |

## 当前构建产物

| 项目 | 数值 |
| --- | --- |
| 完整镜像 | `firmware/luna-panel/build/luna_panel_full.bin` |
| 文件大小 | `1,931,712` 字节 |
| SHA-256 | `74DBF2DDD58D4002C8A010497CAE6B337FA63182CCF55FC1762718CAB631364F` |
