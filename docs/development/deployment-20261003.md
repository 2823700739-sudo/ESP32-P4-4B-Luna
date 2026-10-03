# 2026-10-03 当前修复部署与提交前验证

用户本轮明确授权把此前修复部署，实测后提交未提交代码。本次处理当前工作区的全部既有改动，没有新增产品功能；提交仅到本地 Git，不推送。下面时间均为 Asia/Shanghai。

## 实际部署与设备证据

- 15:03:55 使用 `pc-agent/manage-ble-resident.ps1` 的 Stop/Start 重启本项目常驻，登录自启与配对保留。新 worker PID 53892；Python 虚拟环境 launcher 与真实子进程的创建时间均为本次启动时间。没有创建第二个 BLE 客户端。
- 新进程加载 GPU 无效行过滤、超额显存隔离、连续三次采样失败后重建、本地快照异常隔离，以及当前媒体/额度逻辑。两次 discovery 未发现广播后，15:04:21 完成 authenticated hello/time sync，ATT value 253 bytes；15:04:22 固件接受 CPU=1.6、GPU=5.9、双额度 60/90。
- 15:09:23 已记录 712 次交换、连接 301 秒的 `HEALTHY`；15:09:26 再次接受 CPU=0.5、GPU=5.9、双额度 57/90。恢复认证后此观察窗口没有新增 OFFLINE。部署清单中的全部运行源码哈希仍与重启时一致；常驻保持 Running/Connected。
- 独立只读 `WindowsDashboard` 间隔两秒采样：RX 6600、CPU 0.7%、GPU 6.0%、RAM 14.15/15.78 GiB、VRAM 1.11/7.96 GiB；查询已关闭。温度仍为未知。
- `scripts/build-firmware.ps1 -BleB1 -Action build` 成功，B1 镜像 9,750,448 bytes，SHA-256 为 `a4ebf4273a4e3546d7c73680029fbea4c9ae1fd962fae190805af13b5bc51173`，与 `.tools/flashed-luna-optimized-a4ebf427.bin` 已烧录参考副本一致。此前交接已有 app-only 烧录及整镜像校验记录，本次没有新增固件差异，因此没有重复烧录或 Flash 读回，不声称本轮重新核验板载镜像哈希。
- 当前 PnP 枚举确认 COM27 为 USB-Enhanced-SERIAL CH343。`scripts/observe-standby-power.py --port COM27 --seconds 60` 完成，DTR/RTS 关闭、无写入、错误标记 0、端口释放；ACTIVE=0、STANDBY=0，未发生往返事件，不能作为待机/触摸验收。

原始轮转日志、启动时间与部署源文件 SHA-256 清单保存在本机忽略的 `.tools/deployment-20261003/`；该目录不是发布附件，复制原始日志前需另行审查隐私。

## 提交前验证

| 检查 | 结果及边界 |
| --- | --- |
| `.venv-test` 完整 Python/C 套件 | `python scripts/run-pc-agent-tests.py`：144 项通过，使用本机 GCC/managed components |
| `.venv-ble` 实际运行解释器 | 额度、媒体动作、动作协议、指标及 BLE link 相关 42 项通过；`pip check` 无冲突；测试调用为模拟，不操作真实播放 |
| 预览模型 | `node --test docs/design/preview/model.test.mjs`：27 项通过 |
| 浏览器 | `scripts/test-luna-ui-preview.cjs`：69 项通过；用现有 bundled Playwright、已安装 Edge 和临时回环 HTTP 服务；服务与浏览器结束释放 |
| 原生 UI | Debug 构建真实 LVGL 9.3/固件 UI，`luna_ui_test.exe --double-buffer` 全部断言通过，包含模拟 100 次切卡与 10 次待机返回像素检查 |
| 字体 | `luna_ui_test.exe --font-race`：12,000 次受保护并行解码，corrupted=0 |
| PowerShell | 当前 scripts/pc-agent 入口语法解析通过 |
| 提交范围 | 凭据模式扫描无候选；忽略本地环境、构建、日志、Wi-Fi sdkconfig；旧路线恢复 ZIP 仍存在仓库外 |

首次把未跟踪生成字体纳入索引后，默认 `git diff --cached --check` 报告 8 个 `lv_font_conv` 生成 C 文件末尾多一个空行。保留生成器原始输出及 manifest 哈希；手写文件按默认空白规则检查，生成 C 仅在该命令中豁免 `blank-at-eof`，其他空白错误仍检查，不修改 Git 全局配置。这不影响构建镜像或已部署代码。

第一次预览命令缺少 `NODE_PATH`，第二次默认浏览器不存在；改用现有运行时包目录和已安装 Edge 后通过，没有新安装依赖。第一次原生构建命令缺少 CMake 的 PATH；使用现有缓存记录的 CMake 路径后通过。以上是本地命令环境修正，不是用户纠正。

## 尚未验证

真实 PDH/驱动故障、采集器挂起、本地快照异常、Windows 睡眠/唤醒与重启登录自启未做故障实测；对应故障边界只有回归证据。本次重启加载并正常交换不能替代这些检查。8 小时实机、100 次物理切卡、物理触摸/待机唤醒、内存/栈趋势、电流仪测量、远端 CI、新机器安装及项目许可证选择仍开放。没有操控用户真实播放，没有擦除或修改 C6/NVS/TF，没有推送。

本地提交用于保存已部署和验证的当前工程，不代表发布门槛全部满足。

## 15:47 后续修复统一部署与验证

基线 `36e1354` 后新增的 GPU 数组读取失败恢复和测试入口拒绝跳过两项修复，本轮已按用户原有授权完成部署/验证并纳入后续本地提交。前文“尚未验证”反映 15:07 当时范围，以下增加的证据只覆盖实际列出的场景。

- 完整测试入口 `python scripts/run-pc-agent-tests.py`：150 项无跳过通过。实际 BLE `.venv-ble` 的 Windows 指标、dashboard、BLE link、额度、媒体动作和动作协议相关 45 项通过；`pip check` 无依赖冲突。测试中的媒体动作是 mock，本轮没有发送真实控制。
- 在独立 Windows 进程创建真实 `WindowsDashboard`/PDH query，正常样本 GPU=10.5%、VRAM=1.66/7.96 GiB。保留有效 query，仅将这个进程的 counter 调用参数置为空句柄；`PdhCollectQueryData` 实际成功、格式化 API 实际返回 `0xc0000bbc`。微软[接口说明](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw)明确无效 counter 的错误返回。没有改变 GPU 驱动、其他进程的查询或 BLE 客户端。
- 新路径将 GPU/VRAM 用量设为未知、保留 CPU/RAM、实际关闭 query；立即再采样未高频重建。真实等待 30 秒冷却后重新创建查询，预热两秒取得 RX 6600、CPU 2.0%、GPU 15.2%、RAM 15.01/15.78 GiB、VRAM 1.78/7.96 GiB，并关闭查询。证明真实 API 的受控无效参数恢复，不是自然驱动故障或睡眠恢复。
- 维护者本机复现入口 `.\pc-agent\.venv-ble\Scripts\python.exe .tools/deployment-followup-20261003/check-real-pdh.py`，原始 JSON 结果、部署源码 SHA-256、前后日志和整理前 HANDOFF 副本均在该忽略目录；没有纳入公开发布物，也不当作新机器验证。
- 15:41:16 使用项目 Stop/Start 入口重启常驻，新 worker PID 44916，15:41:34 authenticated hello/time sync 和 dashboard accepted。截至 15:46:37 连续健康 303 秒、606 次交换，15:46:41 接受 CPU=2.5、GPU=15.6、双额度 42/88。部署后曾有单次 Codex 刷新超时，后续额度已恢复；认证后此观察窗口无 OFFLINE。
- 未改固件或 UI，无需重复构建/烧录/浏览器检查。本轮未打开 COM、扫描配对、改变 C6/NVS/TF、操作真实播放或推送。原生/物理触摸、8 小时实机、驱动/睡眠/登录恢复、远端 CI 和新环境验收仍未完成。

交接整理只保留一份当前快照，全部旧阶段证据留在 current-status 与本页；保护原文的本机副本，避免接手同时读到“已部署”和“未部署”而误操作。

最终观察补记：15:49:38 健康 484 秒、928 次交换，15:49:44 接受 CPU=6.0、GPU=15.0、双额度 40/87。15:48:21 又出现 Codex RuntimeError，额度短时变为未知，随后自行恢复；BLE/电脑指标未中断。具体异常原因未确认；不将自动恢复归因于 GPU 修复。源码清单比对未变化，固件镜像 SHA-256 仍与前述一致。

追加观察 15:50:46 额度再次未知，15:51:39 BLE 仍健康（605 秒、1202 次交换）。额度源有间歇失败尚未定位；当前提交不声称解决该故障，后续应先查最新额度和错误来源，不修改账号、凭据或全局代理来猜测修复。
