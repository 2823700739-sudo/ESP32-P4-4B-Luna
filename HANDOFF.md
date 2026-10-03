# Luna 项目交接

更新：2026-10-03（Asia/Shanghai）。这是当前接手快照；逐轮历史留在[当前状态](docs/development/current-status.md)和[部署记录](docs/development/deployment-20261003.md)，不在这里并列多个“最新”状态。

新会话第一句：

> 请先读取 HANDOFF.md，了解项目现状，再继续推进。

## 当前在做什么

工作区为本文件所在仓库，GitHub：<https://github.com/2823700739-sudo/ESP32-P4-4B-Luna>。当前在完成稳定性边界与发布准备，不堆砌功能。

用户已明确授权“把之前的修复的全都部署实现代码没有提交的等实现实测后提交”，并要求继续完善；该部署与验证后本地提交授权持续有效，不必因进入下一轮再确认。不自动推送。额外实机操作、重大产品选择或新权限仍按明确范围处理。

进入每轮依次读取本文件、[当前状态](docs/development/current-status.md)、[质量路线](docs/development/quality-roadmap.md)、[项目经验](docs/development/project-experience.md)，再核对 Git 与运行日志。新的明确用户指令优先；记录与文件冲突先查证。

## 已完成与当前部署

- 基线 `36e1354` 保存了当前 BLE/Wi-Fi 五卡产品、性能优化、素材、测试和旧路线清理，已本地提交、未推送。旧路线恢复 ZIP 仍保留在仓库外。
- 后续 GPU 格式化数组恢复修复：`PdhCollectQueryData` 成功但全部格式化数组 API 失败时，进入已有释放查询/30 秒冷却重建；单类可读数据和正常空数组保留。代码在 `pc-agent/windows_dashboard.py`，模拟回归在 `pc-agent/tests/test_windows_dashboard.py`。
- 测试入口 `scripts/run-pc-agent-tests.py` 的完整/托管两种模式均拒绝意外跳过；托管原生排除仍在执行前完成。三项入口回归在 `pc-agent/tests/test_test_runner.py`。
- 本轮完整 Python/C 150 项无跳过通过；真实 BLE Python 3.10 环境相关 45 项通过，`pip check` 无冲突。上一部署的预览模型 27 项、浏览器 69 项、真实 LVGL 双缓冲及受保护字体 12,000 次并行解码证据见部署记录；本轮没有 UI/固件改动，不重复宣称新实机验收。
- 独立 Windows 进程实际调用 PDH：保留有效 query，只把本进程计数器参数置为无效句柄，API 返回 `0xc0000bbc`；新路径保持 CPU/RAM、关闭 query，真实等待 30 秒后重新查询 RX 6600，GPU 15.2%、VRAM 1.78/7.96 GiB。最终关闭查询。这是实际 Windows API 的受控故障恢复，不是驱动重启/睡眠验收，不操作常驻、设备或真实播放。
- 15:41:16 必要地重启本项目常驻加载 GPU 新修复，worker PID 44916；15:41:34 自动完成 authenticated hello/time sync 并接受真实 CPU/GPU/额度。完整测试入口是开发工具，无需部署到常驻。最终健康与本地提交以 `git log -1 --oneline`、最新部署记录和日志核对。
- 15:49:38 连续健康 484 秒、928 次交换，15:49:44 接受 CPU=6.0、GPU=15.0、额度 40/87。期间两次额度刷新失败曾正确降为未知，随后自行恢复；BLE 与电脑指标继续，原因未知，不为此再重启。
- 后续 15:50:46 额度再次未知，15:51:39 BLE 仍健康（605 秒、1202 次交换）。额度源仍有间歇性失败，不能宣称已解决；下一轮优先核对其最新状态和错误来源，不将其归因 GPU 修复或擅自改账号/代理。

固件基线：ESP-IDF 6.0.2、LVGL 9.3、Hosted 2.12.11；现有 C6 兼容 HCI 路径保留。B1 镜像为 `firmware/luna-panel/build-ble-b1/luna_panel.bin`，9,750,448 bytes，SHA-256 `a4ebf4273a4e3546d7c73680029fbea4c9ae1fd962fae190805af13b5bc51173`。15:07 部署时构建与已烧录参考副本一致；本轮没有固件变动、重烧或 Flash 读回。参考副本 `.tools/flashed-luna-optimized-a4ebf427.bin`，更早稳定双缓冲副本 `.tools/flashed-luna-double-direct.bin`；不当作新机器构建输入。

## 产品与安全边界

- ESP32-P4-4B、720×720 触屏、P4 rev1.3、板载 C6、DC 常亮供电，TF 保留。
- 名为 Luna 的已配对加密 BLE 连接 Windows；设备 Wi-Fi 独立天气，BLE 校时优先、NTP 备用。
- 默认数字时钟，五张左右横滑卡：时钟、固定音乐播放器、天气、Codex 双额度/最近 VS Code 工程、CPU/GPU/RAM/VRAM。保留整屏繁星、像素猫、电脑双圆环与内存/显存条。
- 音量按钮弹出右侧竖条、顶部静音、点条外关闭；使用明确动作，不重放不确定播放控制。测试不得操控用户真实播放。
- 顶部空白或 180 秒无操作进入三针待机时钟，首次触摸只唤醒回原卡。保留睡猫、上浮淡出 ZZZ 和暖心话，亮度不变。
- 固定 360 MHz、DOUBLE_DIRECT 双缓冲；普通动效局部刷新、场景切换必要完整重绘、字体解码互斥。
- 不恢复 OTG、麦克风、语音、封面、旧 HTTP 业务，不启用旧 `Luna PC Agent`。无可靠温度源时显示未知，不编造零值。
- 保护未提交工作、NVS、配对、TF、本地凭据和忽略目录。不擦除、不刷 C6、不 reset，不为整洁清空素材或构建。固件更新只采用已验证 app-only 流程，不直接运行写分区的默认全量 flash 命令。

## 运行与验证命令

```powershell
# 先读现状，不因写文档反复重启常驻
git status --short
git log -1 --oneline
.\pc-agent\manage-ble-resident.ps1 -Action Status
Get-Content pc-agent/ble-link.log -Tail 12

# 完整套件须真实执行所有选中测试，跳过即失败
.\pc-agent\.venv-test\Scripts\python.exe scripts/run-pc-agent-tests.py
# Windows 托管逻辑，明确排除原生项，不连接设备
.\pc-agent\.venv-test\Scripts\python.exe scripts/run-pc-agent-tests.py --hosted
# 实际 BLE 解释器中的相关回归
$env:PYTHONPATH='pc-agent/tests'
.\pc-agent\.venv-ble\Scripts\python.exe -m unittest test_windows_dashboard test_luna_dashboard test_luna_ble_link test_codex_adapter test_windows_media_commands test_luna_actions

# 配置有变化先 reconfigure，纯构建不接触串口
.\scripts\build-firmware.ps1 -BleB1 -Action build
```

常驻入口 `pc-agent/manage-ble-resident.ps1` 的 Install/Start/Status/Stop/Remove；日志 `ble-link.log*`、`ble-resident.log*`、`ble-crash.log*` 均忽略、不提交。`State=Running` 仅表示计划任务运行，`LinkState` 是最近日志摘要，必须同时查原始 `CONNECTED`/`HEALTHY` 与状态接受。必要部署才 Stop/Start，并检查恢复；不启动并行 BLE 客户端。

前台诊断须先停常驻，结束后恢复；`start-ble-link.ps1 -Music -Foreground` 会在常驻 Running 时拒绝。COM27 曾确认 CH343，每次实际使用前重新识别，两个只读观察器均必须传 `--port`；有限时长、DTR/RTS 关闭、结束释放，不能靠历史编号自动打开。本轮未使用 COM。

## 现在卡在哪里与下一步

本轮没有本地技术阻塞。真实 PDH 受控故障恢复及当前代码正常 BLE 运行已验证；不能因此宣布驱动重启或长期验收通过。

1. 先核对 Git 与最新 `Status`/日志，避免沿用旧连接状态。本轮部署完成后无需为文档再重启；工作区若有后来新改动，区分已提交、已加载与未验证范围。
2. 继续质量路线中的明确缺口。发布门槛仍缺：8 小时实机/内存与栈趋势、100 次物理切卡、长期显示与触摸/待机往返、Windows 重启登录自启及睡眠恢复、设备断电/蓝牙异常矩阵。必要时提出具体用户操作和预期信号，不自动重启电脑、设备或驱动，不把模拟次数当实机次数。
3. 新 Windows 环境安装/构建、GitHub Actions 远端运行、待机电流仪测量和项目分发许可证选择仍未完成；许可证不能由助手代用户决定。
4. 每阶段更新本文件、状态、[三列复盘](docs/development/collaboration-retrospective.md)，检查[经验](docs/development/project-experience.md)并更新相似编号；无用户纠正或新增经验时如实说明，不编造记录。达到发布门槛后提交证据给用户决定发布，结束持续优化。

## 已验证不能再走的弯路

- 180 MHz 在现显示配置出现 DSI underrun/蓝屏，不重试；三缓冲局部复制出现旧数字，不恢复。普通脏区优化不能去掉切换时完整重绘。
- 字体损坏曾由并行共享 RLE 状态竞争复现，不盲目归因 RAM；保留互斥。详见[性能证据](docs/development/ui-performance-20261002.md)。
- Kconfig 新符号先 reconfigure；构建路径取显式参数/环境变量，设备入口须显式当前端口，不恢复开发机绝对路径或默认 COM27。
- `Running` 不能当连接健康；旧日志超过 90 秒为 Stale。正常连接恢复不能归因未加载代码，短时健康不能算 8 小时验收。
- GPU 空名、超额显存、数组 API 全失败分别在提供者边界隔离；不能放宽固件严格整包校验。真实 API 受控故障仍不等于真实驱动/睡眠恢复。
- 持续损坏采集器不能永久每 2 秒重试同一对象；连续三次异常后释放并有上限退避重建。只隔离本地快照生成，真正 BLE 请求失败继续断线且不重放。
- 开发测试环境需 pyserial；BLE 运行环境不为全量串口测试加依赖。完整入口不能把 SkipTest 当全套通过；托管预先排除原生项不算跳过，不增加绕过开关。
- Windows Server 的模拟 BLE 测试不能当 Windows 11 桌面实测；原生 LVGL、浏览器或单元测试不能当物理触摸/DSI/长期显示验收。
- 已证实音量条只在松手发最终值，不凭猜测改为拖动洪泛防护；不改变已确认的交互。

## 本地恢复与自动化

仓库外备份相对根目录的位置：`..\Luna-legacy-backup-20261002-1845.zip`、`..\Luna-docs-before-cleanup-20261003.zip`；仅本机恢复用，先检查是否存在，不作为公开构建依赖。本轮接手整理前的 HANDOFF 副本为忽略的 `.tools/deployment-followup-20261003/HANDOFF-before.md`；历次验证以当前状态和部署记录为准。

自动化 ID `luna`，名称“Luna 持续打磨”；此前核对为独立 cron、每小时整点、本地项目、ACTIVE、自动归档。配置可漂移，修改前读取 `$CODEX_HOME\automations\luna\automation.toml`，只能用应用工具更新，保留现有调度/模型/推理/环境/通知字段，除非用户另有要求。不自动另开聊天或迁移模式，每轮依靠文件接力。
