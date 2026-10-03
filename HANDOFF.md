# Luna 项目交接

更新：2026-10-03（Asia/Shanghai）。写给没有本对话上下文的新会话。

新会话第一句：

> 请先读取 `HANDOFF.md`，了解项目现状，再继续推进。

## 当前在做什么

工作区：本文件所在的仓库根目录。GitHub：<https://github.com/2823700739-sudo/ESP32-P4-4B-Luna>。

用户授权持续提升 Luna 的稳定性、流畅度、交互、美观、符合产品路线的创新体验和开源维护质量；不是无限增加功能。2026-10-03 本轮用户明确要求“把之前的修复的全都部署实现代码没有提交的等实现实测后提交”，授权本轮部署和验证后的本地 Git 提交；不授权自动推送。后续额外实机操作、重大取舍或新权限仍按用户指令确认。

当前阶段：显示问题已修复并获前一版用户认可，正在做稳定性边界与发布准备。已移除构建入口的开发机绝对路径与隐式 COM27 端口，修正 CI runner，并在本机隔离环境验证了测试依赖和托管测试；没有增加新业务功能。

最近一轮处理电脑采集器连续失败后的恢复：原循环会永久每 2 秒调用同一损坏对象；现在连续 3 次异常后释放并按 2–30 秒退避重建，瞬时错误保留原对象。隔离环境定向测试 10 项、托管套件 103 项通过。代码尚未部署到运行中的 BLE 常驻；远端 CI 和实机验收仍未进行。

本轮只读复核发现此前连续离线的 BLE 链路已恢复：12:01 任务 `Running`、`LinkState=Connected`，日志到 12:00:54 持续 `HEALTHY`，并有 `DASHBOARD accepted`。独立 Windows 只读采样取得 RX 6600 与 CPU/RAM/VRAM 数值。未触发连接或重启，恢复原因未知；这不能证明尚未部署的采集器重建逻辑或长期稳定性。证据见[当前状态](docs/development/current-status.md)。

最新一轮 13:02 只读状态又为 `Running`、`LinkState=Offline`，日志到 13:02:29 连续 discovery 重试；12:01 的连接记录已过时，设备供电/广播及断线原因未知。`pc-agent/README.md` 已补充连续三次整次采样失败后重建采集器的说明及验证边界；没有修改或部署运行代码。

14:03 本轮对照电脑采样与固件严格校验，发现 PDH 异常/重复显存行合计超过 DXGI 总显存时，固件会拒绝整份电脑状态。现将这种显存用量标为未知，保留其余有效指标；托管 104 项、固件 C 解析器 4 项通过。代码未部署到 BLE 常驻，真实驱动异常与实机效果未验证。14:02 常驻仍 `Running`/`Offline`，日志持续 discovery 重试，设备供电/广播未知。

14:54 本轮把 `pc-agent/luna_ble_link.py` 的本地 dashboard 快照生成异常限制在遥测路径内，保留已认证 BLE 会话和音乐/心跳；真实 BLE 请求异常仍走断线且不重放。模拟连接回归 8 项、托管套件 105 项通过。新版未部署到常驻，真实采集故障与实机效果未验证。只读状态已自然变为 `Running`/`Connected`，14:54:27 有近期 `HEALTHY`，14:53:58 有 `DASHBOARD accepted`；恢复原因未知，不能归因于未部署代码。

**最新部署状态（2026-10-03 15:07，覆盖上文“尚未部署”描述）：** 用户本轮明确授权部署并在实测后本地提交。15:03:55 Windows 常驻已重启加载当前全部 Agent 代码，15:04:21 自动认证连接，随后持续接受真实 dashboard 与健康交换。当前 B1 构建成功，镜像 SHA-256 仍为 `a4ebf4273a4e3546d7c73680029fbea4c9ae1fd962fae190805af13b5bc51173`，与已烧录参考副本一致，因此没有重复烧录。完整 Python/C 144 项、真实 BLE Python 环境相关 42 项、预览模型 27 项、浏览器 69 项、真实 LVGL 双缓冲与受保护字体 12,000 次并行解码通过。COM27 本轮重新通过 PnP 确认为 CH343，60 秒 DTR/RTS 关闭且无写入的观察无错误标记，结束释放；未发生待机/唤醒事件。真实 Windows 正常采样取得 RX 6600 与 CPU/GPU/RAM/VRAM。详细证据见[部署记录](docs/development/deployment-20261003.md)。短时实测不等于真实驱动故障恢复、8 小时稳定性或物理触摸验收。

进入下一轮前依次读本文件、[当前状态](docs/development/current-status.md)、[质量路线](docs/development/quality-roadmap.md)、[项目经验](docs/development/project-experience.md)；有必要再读[性能证据](docs/development/ui-performance-20261002.md)。新用户指令优先；文档与实际文件/配置有冲突时先核对，不直接照旧记录操作。

## 当前产品与必须保留的边界

- 硬件为 ESP32-P4-4B，720×720 触摸屏，P4 rev1.3，板载 C6，DC 持续供电，TF 已接入。
- Windows 通过名为 Luna 的已配对、加密认证 BLE 连接；Wi-Fi 在设备上独立获取天气，BLE 校时优先、NTP 备用。
- 默认数字时钟，五张左右滑动卡片：时钟、固定音乐播放器、天气、Codex 5 小时/周剩余额度与最近 VS Code 工程、CPU/GPU/RAM/VRAM。整屏繁星、像素猫，保留已确认预览的主体设计。电脑状态卡保留双圆环加内存/显存条布局。
- 音乐音量为右侧弹出的竖条，顶部静音；点条外关闭。控制使用明确的播放/暂停等动作，不重放不确定动作。
- 顶部空白或三分钟无操作进入三针待机时钟；首次触摸只唤醒回原卡。睡猫与上浮淡出的 ZZZ 保留，底部一句暖心话；亮度不变。
- CPU 固定 360 MHz，显示使用 DOUBLE_DIRECT 双缓冲，普通动效局部刷新；场景切换仍需完整重绘。保留字体解码互斥。
- OTG/HTTP 业务、麦克风、语音与封面管线已退出；UART 只用于开发。不要恢复旧路线。没有可靠温度源时标记不可用，不编造温度或用零代替缺失值。

## 已经完成了什么

### 前轮固件与清理

只更新可见卡片，切卡/唤醒后读取最新缓存；相同透明度/音乐展示字段不重复绘制，去掉重复标题/歌手缓存。额度采集只保留额度，删除旧工程列表查询；限制响应队列，增加媒体元数据超时和过期排队动作取消。

旧代码与脱离路线的 22 份文档已删除，删除前含未提交内容均备份在仓库外：

- `..\Luna-legacy-backup-20261002-1845.zip`（相对本机仓库根目录）
- `..\Luna-docs-before-cleanup-20261003.zip`（相对本机仓库根目录）

保留 C6 兼容 HCI 路径、B0 诊断、TF 只读工具、字体许可和维护证据；它们不是可随意删除的冗余。

### 本轮电脑端恢复

`pc-agent/luna_dashboard.py`：启动采集失败后以可中断的 2–30 秒退避重试，重复 start 不建多个线程。

`pc-agent/windows_dashboard.py`：显卡 PDH 查询失败独立降级，释放查询，30 秒后重新识别显卡并建立查询；CPU、RAM 与项目采集继续。初始化失败释放查询；数组读取前检查条目数能否落在缓冲区中；close 幂等。

新增 9 项回归，最后一次完整 Python/C-core 测试 137 项通过；实际 BLE Python 3.10 相关回归 37 项通过。独立真实 Windows 采样得到 RX 6600 的 CPU/GPU/RAM/VRAM。没有用真实播放动作做回归，也没有真实重启驱动。

01:29 重启本项目 BLE 常驻加载新代码，三次扫描未发现广播后于 01:30:20 自动认证连接，接受真实状态。交接前日志已记录到 01:41，连续健康交换；这不是 8 小时验收。本轮电脑端优化未修改或烧录固件。

### 本轮收尾流程

创建本文件、[协作复盘](docs/development/collaboration-retrospective.md)、[项目经验](docs/development/project-experience.md)。收尾规范统一在质量路线中；任务提示词也要求每轮执行，避免只依赖当前对话历史。
自动化提示词已更新并读回核对：独立任务方式、每小时整点、项目、模型、推理级别、运行环境与自动归档设置均未改变，ACTIVE 保留。两项文档回归通过（含交接与全部本地链接）；本轮未重新跑完整业务套件，137/37 是上一轮的已记录结果。

### 2026-10-03 上轮独立运行：可移植构建入口

检查发现 `scripts/build-firmware.ps1` 默认绑定开发机的 ESP-IDF/工具绝对路径与 COM27，README 也指向该安装目录。现改为显式 `-IdfPath`/`-IdfToolsPath` 优先，其次 `IDF_PATH`/`IDF_TOOLS_PATH`，工具路径无配置时使用每用户 `.espressif`；`flash`/`monitor` 须明确传入当前端口。README 已写前提并链接 Windows Agent 安装说明。

本机检查：PowerShell 解析、缺失路径/端口与无效显式路径拒绝、文档两项回归通过；现有环境变量下 B1 纯构建成功，镜像 SHA-256 前后相同，为 `a4ebf4273a4e3546d7c73680029fbea4c9ae1fd962fae190805af13b5bc51173`。构建前 BLE 常驻为 Running，日志有真实状态接受与健康交换。本次未刷写、未打开 COM、未重启常驻、未操控播放。新 Windows 环境从零安装构建与显式参数指向另一套真实工具目录尚未验证。详情见[当前状态](docs/development/current-status.md)。

本次没有用户纠正；[协作复盘](docs/development/collaboration-retrospective.md)如实记载。项目经验新增 EXP-003，记录路径优先级与端口显式化的可复用方法。

### 2026-10-03 本次独立运行：托管测试平台

现有未提交的 `.github/workflows/hosted-tests.yml` 使用 `windows-2022`，当前镜像 build 20348；锁定 Bleak 3.0.2 官方支持 Windows 11 build 22000 起。本轮把 Windows Python job 改为 `windows-2025`，当前 Server 2025 镜像 build 26100；`pc-agent/README.md` 明确 Server 上的模拟测试不能证明 Windows 11 桌面 BLE 运行。官方镜像及依赖链接和具体证据见[当前状态](docs/development/current-status.md)。

本机 `pc-agent/.venv` 执行 `python scripts/run-pc-agent-tests.py --hosted`，98 项通过；Node 预览模型 27 项、文档检查 2 项通过。仅装 BLE 运行依赖的 `.venv-ble` 缺 `pyserial`，不能当完整测试环境；测试清单已有 `pyserial`。GitHub Actions 远端尚未执行，全新 Windows 安装也未验证。本轮没有固件改动、烧录、COM 使用、BLE 常驻重启或真实播放操作。

### 2026-10-03 本次独立运行：隔离测试依赖

按 `pc-agent/README.md` 建立 Python 3.10 `pc-agent/.venv-test`，从 `pc-agent/requirements-test.txt` 安装依赖；`pip check` 无冲突、`scripts/run-pc-agent-tests.py --hosted` 的 98 项测试通过。新增 `.gitignore` 的 `pc-agent/.venv-test/`，`git check-ignore` 已确认，防止本地测试环境出现在待提交文件。具体证据见[当前状态](docs/development/current-status.md)。这仍是本机隔离环境验证，不是 GitHub Actions 远端或全新 Windows 安装验收。

只读检查：`Luna BLE Agent` 仍为 Running，但 `ble-link.log` 到 06:02:33 仍持续 discovery 离线；设备供电/广播未知。本轮未接触设备、串口、固件或真实播放；未重启常驻。本轮没有用户纠正；复盘增加无纠正记录，EXP-004 更新了隔离安装证据。

只读检查时 `Luna BLE Agent` 任务为 Running，但 `pc-agent/ble-link.log` 到 05:02:25 连续 `phase=discovery` 离线重试。设备是否通电/广播未知；不要把任务 Running 当作连接健康，也不要无因重启或重新配对。本轮无用户纠正；复盘文件新增“无纠正”的本轮记录，项目经验新增 EXP-004。

### 2026-10-03 本次独立运行：串口观察入口

代码检查发现 `scripts/observe-tf-readonly.py` 与 `scripts/observe-standby-power.py` 还默认使用旧 COM27。现两个脚本都要求操作者传 `--port`，缺参在打开串口前拒绝；README 同步。TF 观察器测试改用模拟测试端口，新增缺参拒绝回归。运行 `pc-agent/.venv-test/Scripts/python.exe -m unittest pc-agent/tests/test_tf_observer.py` 为 6 项通过；`pc-agent/.venv-test/Scripts/python.exe scripts/run-pc-agent-tests.py --hosted` 为 99 项通过。两个脚本无 `--port` 均显示参数错误；`git diff --check` 无空白错误。没有真实端口观察、固件构建/烧录、常驻重启或真实播放动作。本轮无用户纠正；复盘记为无纠正，EXP-003 补充观察器范围。

07:03 只读检查：`Luna BLE Agent` 为 Running，但 `ble-link.log` 截至 07:02:41 仍连续 `phase=discovery` 离线。设备供电/广播未知，不推断根因或冒称连接健康。

### 2026-10-03 本次独立运行：公开说明的本机路径

根 README、`docs/README.md` 和 `docs/development/ui-performance-20261002.md` 原写有开发机仓库外备份的绝对路径，给新环境造成错误定位，也会暴露本机目录。现保留“备份存在且不属于发布物”的事实；本交接文件用相对仓库根目录的路径保留恢复方法，并用 `$CODEX_HOME` 指示自动化配置。两份备份在本机仓库上一级目录均存在，没有移动或覆盖。

文档链接/已退出路线检查 2 项通过；公开 Markdown 中不再出现本机用户目录绝对路径；`git diff --check` 无空白错误（原有换行格式提示仍在）。这不是远端 GitHub 页面、新环境或实机验收。本轮未改固件和运行代码，未构建、刷写、打开 COM、重启 BLE 常驻或操控播放。任务只读状态为 Running，但日志截至 08:02:15 仍连续 discovery 离线，供电/广播仍未知。本轮无用户纠正，复盘如实记载；EXP-003 补充公开恢复路径的方法。

### 2026-10-03 本次独立运行：前台 BLE 诊断入口

`pc-agent/README.md` 原将前台日志命令紧接后台启动列出，可能使操作者在 `Luna BLE Agent` 常驻运行中启动第二进程；共享单实例锁会让前台迅速退出。`pc-agent/start-ble-link.ps1` 现在在前台启动前检查任务状态，运行中明确拒绝；README 给出停常驻、前台诊断、结束后恢复的顺序，并提示期间电脑状态与音乐同步暂停。

本机 PowerShell 解析通过；常驻任务 Running 时调用 `-Music -Foreground` 得到预期拒绝，未启动第二 BLE 客户端。文档 2 项回归通过、`git diff --check` 无空白错误（原有换行提示仍在）。本轮没有停用/重启常驻、打开 COM、构建或刷写固件、连接设备或操控真实播放。09:01:13 只读日志仍连续 discovery 离线，供电/广播未知；停常驻后的实际前台诊断/恢复尚未验证。本轮无用户纠正；复盘如实记录，经验新增 EXP-005。

### 2026-10-03 本次独立运行：常驻链路状态可观测性

`pc-agent/manage-ble-resident.ps1 -Action Status` 原只报告计划任务 `Running`，但日志到 10:00:47 连续报 `phase=discovery` 离线。现在同时报告从最近链路日志事件推得的 `LinkState`：`Connected`、`Offline`、`Starting`、`Stale`、`Unknown` 或 `Stopped`。90 秒以上的旧连接事件不再显示为在线；这仍是只读日志摘要，不是实时扫描或连接证明。`pc-agent/README.md` 已解释字段。

本机 `.venv-test` 常驻相关 5 项测试、托管套件 100 项通过；实际状态命令得到 `State=Running`、`LinkState=Offline`。`git diff --check` 无空白错误（已有换行提示）。本轮未停用/重启常驻、打开 COM、构建/刷写、配对或操控播放。设备供电/广播仍未知。无用户纠正；复盘如实记录，新增经验 EXP-006。

### 2026-10-03 本次独立运行：GPU 无效实例名隔离

`pc-agent/windows_dashboard.py` 中 PDH 计数项若含空实例名，会在 GPU 引擎或显存计算时抛异常，导致本次 CPU、RAM、项目状态一并丢失。现在在 PDH 边界过滤空名，并让 GPU 行计算忽略无效名称和非有限数值；有效 GPU 行仍正常计算。`pc-agent/tests/test_windows_dashboard.py` 新增两项回归，分别覆盖空名原始计数项及混合有效/无效 GPU 行。

本机 `pc-agent/.venv-test/Scripts/python.exe -m unittest pc-agent.tests.test_windows_dashboard` 7 项通过；`pc-agent/.venv-test/Scripts/python.exe scripts/run-pc-agent-tests.py --hosted` 102 项通过；`git diff --check` 无空白错误（仅已有换行提示）。这是模拟故障边界，不是实际 PDH 故障复现。未重启常驻、打开 COM、构建/刷写、配对或操控真实播放。11:05:36 日志仍持续 `phase=discovery` 离线，`State=Running`、`LinkState=Offline`；供电/广播未知。无用户纠正，复盘如实记录，项目经验更新 EXP-002。

### 2026-10-03 11:29：当前工作区测试复核

在已创建且被 Git 忽略的 `pc-agent/.venv-test` 中运行 `pip check`，无依赖冲突；`scripts/run-pc-agent-tests.py --hosted` 102 项通过，不带 `--hosted` 的完整 Python/C 套件 141 项通过。文档检查 2 项、预览模型 27 项通过。完整套件依赖本机 GCC 与现有 managed components；远端 GitHub Actions、全新 Windows 安装和设备实机均未验证。这轮只读取 Agent 状态和日志，未重启常驻、使用 COM、构建/烧录、重新配对或操控播放。复盘无用户纠正；项目经验无新增，沿用 EXP-004 的测试环境方法。

### 2026-10-03 11:48：连续采样失败后的重建

`pc-agent/luna_dashboard.py` 的 `_loop()` 原本只记录 `collector.sample()` 异常并继续调用同一对象，持久故障无法回到初始化路径。现连续 3 次异常后关闭该对象、置空，按已有 2–30 秒可中断退避重新构造；成功采样重置计数和退避。`pc-agent/tests/test_luna_dashboard.py` 新增模拟持久故障恢复回归，定向 10 项与托管 103 项通过；`git diff --check` 无空白错误，仅原有换行提示。真实驱动/睡眠故障、BLE 常驻部署、远端 CI 与实机均未验证。

只读检查时 `Luna BLE Agent` 为 Running、`LinkState=Offline`，日志到 11:47:04 仍持续 `phase=discovery` 离线；设备供电/广播未知。本轮未重启常驻、打开 COM、构建/刷写、重新配对或操控真实播放。无用户纠正，复盘如实记录；EXP-002 更新连续采样失败的恢复方法。

### 2026-10-03 12:03：链路恢复与真实 Windows 数据路径复核

12:01 的只读 `manage-ble-resident.ps1 -Action Status` 显示任务 `Running`、`LinkState=Connected`。`ble-link.log` 到 12:00:54 连续记录 `HEALTHY`，到 12:00:30 有 `DASHBOARD accepted`。独立 `WindowsDashboard` 间隔 2 秒只读采样得到 RX 6600、CPU 0.7%、GPU 5.9%、RAM 14.34/15.78 GiB、VRAM 0.99/7.96 GiB，并已关闭查询。没有重启常驻或触发连接，恢复原因未知；本次正常采样不等于故障恢复或 8 小时验收。未动固件、COM、配对、真实播放。无用户纠正；项目经验无新增，沿用 EXP-002 和 EXP-006。

### 2026-10-03 13:03：Windows Agent 恢复说明与链路复核

`pc-agent/README.md` 补齐了现行采集器连续三次整次采样失败后的关闭、退避重建及瞬时失败语义，并明确模拟回归与尚未部署的边界。文档检查 2 项通过，`git diff --check` 退出码 0（仅既存换行提示）。未改运行代码；未重启常驻、打开 COM、构建/刷写、配对或操控真实播放。

13:02 状态为任务 `Running`、`LinkState=Offline`，日志到 13:02:29 持续 discovery 重试；12:01 的连接健康不再代表当前状态。设备供电/广播及断线原因未知。无用户纠正，复盘如实记录；项目经验无新增，EXP-002 已包含该恢复方法。

### 2026-10-03 14:03：超额显存读数隔离

`pc-agent/windows_dashboard.py` 现在只在 PDH 显存用量合计不超过 DXGI 总量且总量为正时发布用量；重复/异常行造成的超额值改为未知，避免 `firmware/luna-panel/main/luna_dashboard.c` 将 CPU/GPU/RAM/项目一并拒绝。`pc-agent/tests/test_windows_dashboard.py` 新增模拟重复行回归；`pc-agent/tests/test_luna_dashboard_c.py` 增加固件超额值拒绝案例。隔离 Python 3.10 环境 `scripts/run-pc-agent-tests.py --hosted` 104 项、`python -m unittest pc-agent.tests.test_luna_dashboard_c` 4 项通过，`git diff --check` 无空白错误（仅既有换行提示）。

14:02 只读任务状态为 `Running`/`Offline`，日志到 14:02:38 持续 discovery 重试；供电/广播与原因未知。没有重启常驻、打开 COM、构建/刷写、配对或操控播放。新 Windows Agent 代码尚未部署；模拟行不等于真实驱动故障或实机验收。本轮无用户纠正；复盘如实记录，项目经验更新 EXP-002。

### 2026-10-03 14:54：本地状态快照异常隔离

`pc-agent/luna_ble_link.py` 原将 `dashboard.snapshot()` 本地异常交给连接外层，误触 BLE 断线退避。现仅捕获本地快照生成异常，记录类型并按两秒节奏再尝试，真实 `session.request()` 失败仍退出会话且不重放。`pc-agent/tests/test_luna_ble_link.py` 新增首次异常后同一会话恢复回归；本机 `.venv-test` 定向 8 项、托管 105 项、文档 2 项通过，`git diff --check` 无空白错误（仅既存换行提示）。本轮未部署、停启常驻、打开 COM、构建/刷写、配对或操控真实播放。无用户纠正；复盘如实记录，新增经验 EXP-007。实测本地故障、远端 CI 和实机验收仍未验证。

## 当前运行与操作入口

固件：ESP-IDF 6.0.2、LVGL 9.3、Hosted 2.12.11；现有 C6 需要兼容同步路径，不自动升级 C6。

当前 B1 镜像为 `firmware/luna-panel/build-ble-b1/luna_panel.bin`，前轮核对文件 SHA-256 为 `a4ebf4273a4e3546d7c73680029fbea4c9ae1fd962fae190805af13b5bc51173`，9,750,448 bytes。更早日志记录该镜像已 app-only 烧录并整镜像校验；本轮没有重新核对镜像或读回板载 Flash。已烧录参考副本 `.tools/flashed-luna-optimized-a4ebf427.bin`；更早稳定双缓冲副本 `.tools/flashed-luna-double-direct.bin`。

Windows 常驻 `Luna BLE Agent` 于本轮 15:03:55 重启加载全部当前 Agent 代码，15:04:21 自动认证连接，15:06:21 有 `HEALTHY`、15:06:23 有真实 `DASHBOARD accepted`；GPU 容错、超额显存隔离、连续采样重建和本地快照异常隔离均已加载，历史“尚未部署”段落仅代表当时状态。独立故障注入仍是模拟验证，不宣称真实驱动故障恢复。本项目 Agent 和 Codex 定时任务不同；`ble-link.log` 为轮转日志，`ble-resident.log`/`ble-crash.log` 为辅助日志，不提交。旧 `Luna PC Agent` 保持停用。

```powershell
# 正常诊断先只查看，不为更新文档重启服务
.\pc-agent\manage-ble-resident.ps1 -Action Status
Get-Content pc-agent/ble-link.log -Tail 12

# 完整回归用开发环境（含只读串口工具的测试依赖）
.\pc-agent\.venv\Scripts\python.exe -m unittest discover -s pc-agent/tests

# 真实 BLE Python 3.10 环境的相关回归
$env:PYTHONPATH='pc-agent/tests'
.\pc-agent\.venv-ble\Scripts\python.exe -m unittest test_codex_adapter test_windows_media_commands test_luna_actions test_luna_dashboard test_windows_dashboard test_luna_ble_link

# 固件，配置有变化先 reconfigure
.\scripts\build-firmware.ps1 -BleB1 -Action build
```

当前调试串口曾核对为 COM27/CH343；未来实际使用前重新识别，不凭旧编号打开端口。不要运行长期观察器或并行 BLE 客户端。有限观察使用 `scripts/observe-standby-power.py`，DTR/RTS 关闭，结束必须释放端口。
`scripts/observe-standby-power.py` 和 `scripts/observe-tf-readonly.py` 现在都必须传 `--port <当前端口>`；不传时直接拒绝，不会创建串口对象。

本仓库有大量未提交改动、删除项和新文件，均需保留。不要 reset、覆盖、批量删除忽略目录或为“整洁”清空构建/素材。Wi-Fi 凭据在忽略配置内，不输出、不提交。

## 现在卡在哪里

本轮部署和提交没有技术阻塞：15:09:23 新 Agent 连续认证连接 301 秒、712 次健康交换，15:09:26 仍接受真实状态；本地提交是在此短时实测及回归后保存当前源码，远端和长期门槛继续开放。下面早前观察仅作为历史，不覆盖本轮部署。

没有阻止继续做本地优化的技术阻塞。BLE 链路 14:02 离线、14:54 已恢复近期健康交换；供电/广播及变化原因未经核实，后续先读实时状态，不猜测根因。发布门槛尚未完成：8 小时实机运行、内存/栈趋势、100 次实机切卡、Windows 重启登录自启及睡眠/唤醒恢复矩阵、当前优化版物理触摸长期验收。若要开展这些检查，需安排明确的实机步骤，不能自动重启用户电脑或把模拟次数当作实机次数。

待机总功耗尚未用电流仪测量，不声称具体下降比例；项目分发许可证、新环境构建安装和 GitHub Actions 远端运行仍待验证。托管测试入口已在本机隔离环境通过。
本次构建入口仅在当前机器及现有 IDF 工具链验证；新环境从零安装/构建仍属于发布门槛。无需为此操作当前设备。

## 下一步准备怎么做

1. 先核对 Git 状态、文档与当前 `Status` 的 `State`/`LinkState` 及原始日志；14:54 的连接状态会变化，不沿用为下一轮结论。若离线，先确认设备是否通电/广播再决定是否需要恢复步骤，不自动重新配对。`LinkState` 是日志摘要，不代替实机连接验证。
2. 当前全部 Agent 修复已部署并短时实测；无需为补文档再次重启。用户本轮授权本地提交，提交范围包含此前未提交的当前 BLE/Wi-Fi 五卡产品、稳定性修复、测试、素材、文档和已备份的旧路线清理。下一轮先用 `git log -1 --oneline`、`git status --short` 核对提交与工作区，再决定是否有新的明确问题；远端 CI、新环境和实机发布门槛尚未完成。不自动推送或增加产品功能。
3. 为改动补回归并在相应运行环境验证；保持当前 UI 和固件基线。需要部署时记录前后版本与恢复结果。
4. 有需要用户配合的实机验收时提供具体操作、预期与失败信号；否则继续推进。
5. 每完成一部分或遇到阻塞，更新本文件、做三列复盘、检查并去重项目经验，报告文件位置与经验条目编号。
6. 公开说明不依赖本机仓库外备份的绝对地址；如需本机恢复，先按本文件的相对路径核对副本，不把副本当作构建输入。

## 已经验证不能再走的弯路

- 在当前显示配置中动态降到 180 MHz 曾出现 DSI underrun/蓝屏；不重试相同降频策略。不把“亮度不变的待机”解释为必须降频。
- 三缓冲局部复制路径出现旧时钟数字残留；已经换成稳定双缓冲直接绘制。普通脏区域优化不能删除切换时必要的完整重绘。
- 字体马赛克不能直接归因于 RAM 不足。真实并行字体试验复现了共享解码竞争；受互斥保护版 12,000 次试验无损坏，不移除保护。
- Kconfig 更改后只 build 会沿用旧生成配置；先 reconfigure。
- 构建脚本写死开发机 IDF/工具路径与默认 COM27 会误导新环境与误触设备；本次已改为显式参数/环境变量与显式端口，不恢复这些机器私有默认值。
- 两个有限只读串口观察器也曾默认 COM27；现在必须显式提供当前端口，不再依赖历史端口编号。
- 旧备份真实存在，但其开发机绝对路径不应放进公开上手说明；使用相对仓库根目录的本地交接位置，并在新机器上先检查是否存在，不猜测可用。
- Windows Server 2022 runner build 20348 低于锁定 Bleak 文档列出的 Windows 11 build 22000；不要把该旧 runner 当成兼容性验证，Server 2025 模拟测试也不能取代 Windows 11 真实 BLE 验收。
- 模拟触摸、浏览器预览、本地构建与 UART 日志不是实机物理触摸/长期稳定性证明。
- 全量串口观察工具测试不能强塞进不带 pyserial 的 BLE 环境；分别验证开发环境与实际运行环境。
- 测试专用 `.venv-test` 必须被 Git 忽略；已从测试清单安装并通过本机托管套件，不必再用缺依赖的运行环境重复试跑。
- 已知无需重复做的“优化”：音量条只在松手时发送最终值，未证实拖动发送洪泛；不要凭猜测改动作语义。
- 前台 BLE 诊断不能在常驻任务运行中直接启动；现在入口明确拒绝，并指向停常驻、诊断、恢复顺序。不要把单实例锁造成的快速退出误判为设备故障。
- 计划任务 `Running` 只能证明守护进程状态；链路可能仍在 discovery 离线。`Status` 现在显示独立的日志链路状态，超过 90 秒的旧事件视作 `Stale`，不把旧 `HEALTHY` 当成当前在线。
- GPU PDH 空实例名曾可让整个电脑状态采样报错；现在过滤无效 GPU 行。模拟回归通过不等于真实驱动故障验收，也不能因当前 BLE 离线就把新代码称为已部署。
- PDH 显存行合计可能超过 DXGI 总显存；固件会拒绝这样的整包状态。现将超额用量标为未知以保留其他指标，不能放宽固件严格校验或把模拟重复行当作真实故障验收。
- 本地 dashboard 快照生成异常不应被当作 BLE 请求失败而断开已认证会话；只隔离生成步骤，真正的发送/应答失败继续走不重放断线流程。模拟同会话恢复尚不等于真实采集故障或部署验收。
- 采集器持续抛异常不能永远对同一对象做 2 秒重试；连续 3 次后须关闭并走有上限的初始化退避。只用模拟恢复验证不能宣称已在当前常驻或真实驱动恢复中生效。
- 离线记录不是永久状态；本轮未干预即观察到近期 `HEALTHY`。不能把自然恢复归因于尚未部署的代码，也不能把十余分钟健康交换充当 8 小时验收。

## 定时任务与新会话约定

Codex 自动化 ID `luna`，名称“Luna 持续打磨”。本轮读取到的实际配置是独立 cron、每小时整点、ACTIVE、本地项目模式、自动归档运行；不再是最初创建的 heartbeat。不要按旧对话假设模式，修改前读实际配置。

配置位置：`$CODEX_HOME\automations\luna\automation.toml`，只能用应用的自动化更新工具修改，不手写 TOML。保留调度、模型、推理级别、项目、归档和通知设置，除非用户另有指令。新运行必须先读本文件。未来用户另开新会话时，可直接使用顶部那句启动指令，无需另建会话来完成交接。

用户本轮说明担心长对话导致偏离。建议并保持独立运行＋文件接力，而不是搬运全部旧聊天；每轮交接及时更新、先查现状，可减少旧需求干扰，但不能保证自动避免一切偏离。
