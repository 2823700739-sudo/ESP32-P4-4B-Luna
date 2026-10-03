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
