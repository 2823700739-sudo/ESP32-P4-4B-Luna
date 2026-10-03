# Luna 项目交接

更新：2026-10-04（Asia/Shanghai）。这是当前接手范围；历史部署证据见[当前状态](docs/development/current-status.md)与[部署记录](docs/development/deployment-20261003.md)。

新会话第一句：

> 请先读取 HANDOFF.md，了解项目现状，再继续推进。

## 当前在做什么

用户最新明确要求：检查 GitHub 发布前重要缺口，**先不继续优化**，写项目说明与开发技术文档，删除无用文档。本轮已完成文档整理与范围审计，停止功能开发。自动化 luna 实际配置已核对为 PAUSED，不因仍有缺口自动恢复。

仓库：<https://github.com/2823700739-sudo/ESP32-P4-4B-Luna>。此前“修复部署、实测后提交”的本地提交授权仍有效；用户于 2026-10-04 明确要求“把这个项目推送到github上”，本轮获准推送当前已有分支 codex/usb-r2。未授权合并 main、建立 Release 或恢复优化；后续仍不自动推送。每轮先读本文件、[状态](docs/development/current-status.md)、[质量路线](docs/development/quality-roadmap.md)、[经验](docs/development/project-experience.md)，新指令优先，冲突先查证。

## 已完成与文件入口

- [README.md](README.md)：定位、五卡功能、网页示例截图、硬件/版本、构建/安装、已知限制与许可。
- [开发文档](docs/DEVELOPMENT.md)：架构、技术栈、模块、BLE 安全/协议、Windows 数据、天气/时间、显示资源、构建/测试与维护。
- [文档索引](docs/README.md)：区分公开说明、当前证据与历史维护记录。更新猫素材来源说明，去掉已过期的“尚待确认”阶段描述；保留原图、哈希和生成提示词。
- 删除四份已被当前说明取代的文档：产品草案 `docs/ideas/luna-desktop-companion.md`、UI 参考 `docs/design/ble-ui-references.md`、U0 迭代说明 `docs/design/u0-ui-preview.md`、中间数据集成 `docs/development/dashboard-clock-20261002.md`。独有 BLE/C6/TF 证据、性能/部署记录、预览、素材和许可证继续保留。
- 代码基线 `36e1354`；后续 `8d14338` 已保存 GPU 数组 API 失败冷却恢复与测试入口拒绝意外跳过。此次仅文档和截图，不修改业务、固件或运行配置。最新文档提交与工作区以 `git log -1 --oneline` / `git status --short` 为准。

## 部署与证据边界

2026-10-03 15:41:16 常驻加载 GPU 修复，15:41:34 authenticated hello/time sync；16:02:13 日志 HEALTHY 2662 次交换、1239 秒连接，16:01:53 接受 CPU=1.0、GPU=5.9、额度 37/87。这是该时刻只读证据，不当作 2026-10-04 的当前在线状态。

前轮完整 Python/C 150 项无跳过、实际 BLE Python 3.10 相关 45 项与依赖检查通过。实际 Windows PDH 受控无效 counter 返回 `0xc0000bbc`，释放 query 并真实等待 30 秒后恢复 GPU/显存；不等于驱动/睡眠恢复验收。网页模型 27、浏览器 69、真实 LVGL 双缓冲/并行字体 12000 次为先前批次，详见部署记录，本轮不重复算作新实机验收。

固件基线 ESP-IDF 6.0.2、LVGL 9.3.0、Hosted 2.12.11。当前 B1 镜像 `firmware/luna-panel/build-ble-b1/luna_panel.bin`：9750448 bytes，SHA-256 `a4ebf4273a4e3546d7c73680029fbea4c9ae1fd962fae190805af13b5bc51173`；本轮没有固件变动、构建、重烧或读回。参考副本在忽略的 `.tools/`，不是新环境构建输入。

本轮文档检查与公开文件扫描结果见当前状态。没有常驻重启、COM 使用、配对、NVS/TF 操作或真实播放控制。

## 重要缺口、卡在哪里与下一步

文档整理无技术阻塞，产品发布门槛尚未全部完成。**已按用户明确指令推送 GitHub，完成后等待新指令，不能自动继续优化。** 可提交开发预览源码，不能宣称已完成完整发布验收。

1. 首次天气地点配置缺失：`luna_weather_set_location()` 仅定义/声明、没有调用入口。设备已有 NVS 地点可运行，空白设备会等待地点；不清空 NVS 做验证，不复活旧天气 HTTP 工具。
2. Wi-Fi 凭据为本地构建配置，没有运行时配网；时区固定北京时间。不要发布含个人凭据的镜像。
3. 额度源曾间歇 TimeoutError/RuntimeError，未知后可恢复，原因未定位；不归因 GPU、不猜测更改账号或代理。
4. 新 Windows 安装/构建、远端 Actions、首次设备安装、8 小时实机/内存与栈趋势、100 次物理切卡、触摸待机、登录自启/睡眠唤醒/设备断电/蓝牙异常矩阵及待机电流仍未验收。
5. 仓库整体 LICENSE 未选择。保留字体 OFL 与第三方许可，许可证需用户决定，助手不代选。

## 维护命令与部署范围

```powershell
# 只读现状；没有部署需要时不反复重启
git status --short
git log -1 --oneline
.\pc-agent\manage-ble-resident.ps1 -Action Status
Get-Content pc-agent/ble-link.log -Tail 12

# 文档回归，无设备操作
.\pc-agent\.venv-test\Scripts\python.exe -m unittest discover -s pc-agent/tests -p test_current_product_docs.py
# 前轮完整/托管入口；文档整理不需要重复全套
.\pc-agent\.venv-test\Scripts\python.exe scripts/run-pc-agent-tests.py --hosted
# 配置变化先 reconfigure，纯构建不访问串口
.\scripts\build-firmware.ps1 -BleB1 -Action reconfigure
.\scripts\build-firmware.ps1 -BleB1 -Action build
```

常驻入口 Install/Start/Status/Stop/Remove；State=Running 不等于 Connected，要查近期 CONNECTED/HEALTHY 和数据接受，摘要超过 90 秒为 Stale。必要加载业务修复才 Stop/Start；前台诊断先停止常驻，结束恢复，不开第二 BLE 客户端。旧 Luna PC Agent 停用。

已验证现有 B1 app-only 更新偏移 0x20000，10 MiB 分区；不把写 bootloader/分区的全量 flash 当维护升级。不擦除设备/NVS/配对/TF、不刷 C6。串口入口必须显式传当前端口，历史 COM27 仅为曾用编号；只读观察有限时长、DTR/RTS 关闭、结束释放。首次部署和真实恢复操作需要具体授权范围。

## 产品边界与不能再走的弯路

保留整屏繁星、像素猫、五卡横滑、Windows BLE、设备 Wi-Fi 天气、三分钟/顶部空白待机、亮度不变、360 MHz/DOUBLE_DIRECT。音乐用六个明确动作，不重放不确定动作，不为测试操控真实播放；没有温度来源时显示未知。不恢复 OTG、麦克风、语音、封面或旧 HTTP 产品。

180 MHz DSI underrun/蓝屏、三缓冲残影不重试；保留普通脏区与场景完整重绘。字体共享 RLE 并发竞争已有证据，保留互斥。GPU 异常在提供者边界隔离，不能放宽固件整包校验；恢复退避与采样频率分开。完整入口拒绝意外 SkipTest；托管模拟、原生 LVGL 和短时在线不能算物理长期验收。细节见[经验](docs/development/project-experience.md)。

## 恢复副本与收尾

本轮完整文档备份在仓库外 `..\Luna-release-docs-backup-20261003-160531.zip`，SHA-256 `BF823D643524507087D2B1318E578FA8345E2F81858B4510858AC34BC2FC4DC6`；删除前核验四份原件均在 ZIP 内。更早 `..\Luna-legacy-backup-20261002-1845.zip` / `..\Luna-docs-before-cleanup-20261003.zip` 继续保留。仅供本机恢复，不作为公开构建依赖或发布附件。

本轮[复盘](docs/development/collaboration-retrospective.md)将“暂停优化、转向发布文档”记作后来明确的需求变更，无用户指出的技术判断错误；[经验](docs/development/project-experience.md)新增 EXP-008 首次启动/NVS 隐式前提审计。后续阶段仍按质量路线同步三份记录与状态。不自动另开聊天、迁移模式或推送。


## 2026-10-04 GitHub 推送收尾

工作区开始时干净，当前分支 codex/usb-r2；fetch 后远端无新增、本地领先 3 提交（36e1354、8d14338、d584389）。对这三个提交的 416 个文本快照进行私钥/token/非空 Wi-Fi 配置模式检查，零候选；只推送 Git 跟踪内容，不包括忽略配置、日志、构建或仓库外备份。非强制推送成功，git ls-remote 核对远端 refs/heads/codex/usb-r2 为 d5843898c53e527012e58580ac6d872528ffa17f，与本地项目提交完全一致；本次收尾记录另行提交并推送同分支，最新完整 SHA 以远端核对为准。无需重新构建、重启常驻或操作设备。

本轮无用户纠正，新增明确推送授权属于范围扩展；复盘已记录。项目经验无新增，沿用 EXP-003 的公开配置边界与 EXP-004 的证据分层，不为推送制造技术条目。功能与硬件验收缺口保持原状；优化/自动化继续暂停。
