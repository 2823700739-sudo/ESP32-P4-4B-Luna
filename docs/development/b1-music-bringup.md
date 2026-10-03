# B1：时钟、固定音乐播放器与 Wi-Fi 天气实机阶段

维护证据：下方为当前 BLE 产品的分阶段实机记录，不将早期未接入项当作当前状态。
完整当前范围、显示路径、待机策略和未验收项见[current-status.md](current-status.md)。

日期：2026-10-02。依据用户允许持续自主开发，在 B0 MTU/资源和 Wi-Fi HTTPS 共存门槛通过后实施。

## 本阶段范围

- `-BleB1` 独立生成配置/构建目录，复用已测 mbedTLS SC/MITM 与绑定 GATT；B0 保留可回退。
- 默认数字时钟，固定唱片音乐卡，720×720 屏幕、590×450 卡片、整屏繁星背景。
  本次按获批网页开放五卡外观；额度/电脑指标仍未接入，不将视觉占位当成数据功能完成。
  宠物和待机表盘已迁移，物理屏幕视觉与交互仍待用户验收，具体见下方增量记录。
- 实际 Windows GSMTC 歌名/歌手/状态、CoreAudio 系统音量；禁止 thumbnail 读取，协议无 cover。
- `state_snapshot` → `state_snapshot_result` 每次最多携带一个 `action_request`；
  `action_result` → `action_result_ack`。复用版本、会话、启动 ID、4 KiB 分片/CRC、8 秒整次超时。
- 板端 8 项 FIFO、6 秒过期；每次点击生成 boot-id + 单调编号。
  断连/host reset 清队列。已提供给 PC 的动作即移出队列，回复或 ack 丢失后也不重放。
- Agent 逐项执行白名单明确命令，共用已存在 ActionHistory 思想（独立实例缓存）。
  失败/超时不追加媒体键、不跨会话补发；断连先于调度时丢弃此次动作。
- 播放按钮立即读取本地目标，所以两次极快点击也生成 play→pause，不等下次 100 ms UI timer。
  旧 PC 状态不覆盖最新待确认目标；完成后最多 3 秒对齐真实状态，不能永久伪造播放/音量。
- 同步读音量及动作均放在 worker thread，不占用 BLE event loop；没有 USB/PC HTTP 初始化。
  新请求 ID 达到 65000 时主动新建会话，避免约长时间运行后耗尽 16 位消息编号。
- 配对/旧绑定恢复 UI 作为顶层独立遮挡页，仅实际确认或恢复请求期间出现；不自动批准。

## 软件证据

- 新增真实 C 核心的 native 单线程测试（只模拟 mutex/time，使用实际 cJSON）。
  覆盖 FIFO/容量、两个点击、旧权威状态、音量反馈、取出后不重发、过期、断连、字段拒绝。
  这些测试不代表 FreeRTOS 调度或真实触屏验收。
- Python mock 覆盖无封面、UTF-8 边界、严格白名单/启动域、快速动作、去重/未知/冲突、
  旧 B0 能力拒绝、ack 失败不重放；新增媒体测试证明 cover-disabled 不访问 thumbnail 属性。
- `.venv-ble` 已安装锁定的 WinRT Media.Control 3.2.1，pip check 通过。
  真实只读适配器检查：媒体会话 available/controllable=true、系统音量 available=true，cover bytes=0。
  未因只读检查播放、切歌或改音量。

## 构建与硬件验收

- 独立 B1 reconfigure/build/P4 flash/hash 校验通过，app 1,965,680 字节（0x1dfe70），SHA256：
  `7DE7B57987E72FD7CDB7A7346036730A9DE2FCDC72B19FCA27E033FB8B71A201`。
  P4 编译的 core/UI 已加入，NimBLE host 栈 6144、LVGL 栈 16384 字节。
  未改写 C6，未擦 NVS/绑定，未启动 USB 或旧 PC HTTP。
- 一次 UART RTS 重启后，LVGL/触摸初始化成功，既有绑定恢复 SC/MITM、key_size=16；
  B1 hello/music 能力及校时成功，无需新的数字比较。有限 55 秒日志未见 panic/复位循环。
- 真实 GSMTC/CoreAudio 状态经 BLE 写入 60/60；传输交换中位 281.5 ms、p95 406 ms、
  max 469 ms，不含每次 250 ms 的测试间隔。屏幕动作计数为 0，没有自动播放/切歌/改音量。
  此结果证明真实数据写入，不证明物理按钮或播放器动作延迟。
- 同时设备 Wi-Fi/HTTPS 刷新真实天气，最终 wifi_online/weather_configured/weather_available=true，
  weather_cached=false；新媒体适配器 cover bytes=0。
- 最终字节诊断：internal_free=175203、internal_min=115635、host_stack_free=3408、
  tx_stack_free=2340；可继续后续原型，但不是完整五卡/宠物预算或长期内存验收。
- 软件完整回归 115 项通过，含 9 个真实 C 核心 native 测试、9 个 Python 音乐业务测试。
  三个 PowerShell 脚本解析和 pip check 通过。
- 有限 UART/BLE 观察器已正常结束并释放 COM27。随后启动独立 `start-ble-link.ps1 -Music`
  常驻入口（launcher PID 20756；进程树不等于多个会话）。旧日常 Agent PID 21400 未停止；
  新入口不打开 COM28，也未安装登录自启。
  后台 15:04:53 完成认证/hello/校时，15:05:23 记录同一连接 30 秒、58 次业务交换，未见 OFFLINE；
  worker PID 13104 为上述 launcher 的子进程，不是第二个独立实例。后台保持运行供用户测试。
- **初次待用户物理验收（后续反馈见下）**：屏幕默认时钟/中文/布局、切到音乐、播放键短间隔双击后回到原状态、
  上/下一首、系统音量、音量面板外点击关闭且其他按钮仍生效。没有将这些项勾为完成。

### 用户测试反馈（同日）

用户针对上一轮触屏/快速播放与音量测试回复“没有异常”。记为用户报告通过，不伪造动作耗时。
后台日志独立记录 15:08 的多次 play/pause 与 previous 均 accepted/result_known=true、duplicate=false；
这支持命令处理通过，播放器最终表现仍以用户反馈为依据。继续迁移设备 Wi-Fi 天气卡。

## B1 增量：设备天气与独立校时

- 天气卡直接读取设备 Wi-Fi/HTTPS 的状态，显示地点、温度、天气、高低温、体感、湿度、
  风速（m/s）及更新时间；天气图形随条件切换，云朵缓慢浮动。不经 PC HTTP，也不经 BLE 接收天气。
- Open-Meteo 请求显式使用摄氏度及 m/s，并验证响应单位、数值范围、时间字段和完整 JSON；
  可选降雨概率缺失不伪造为 0，未知天气代码不伪造成晴天或雨天。
  参数依据：[Open-Meteo 官方文档](https://open-meteo.com/en/docs)。
- 已有地点和基础天气 NVS 布局不变，不清除绑定/缓存。新 typed metrics 仅在 RAM 保存：
  重启后旧缓存仍显示温度/条件/时间，但详细指标显示 `--`，直到新的 HTTPS 请求成功。
  运行期间掉线保留上次成功数据并标记缓存；无缓存时明确等待，而非显示示例天气。
  天气按已有策略每 20 分钟刷新，失败后约 60 秒重试。首次配置/修改地点的 BLE 设置接口仍待实施。
- `luna_time` 统一仲裁 B1 系统时间：每次有效 BLE 时间优先，最近 BLE 校时 120 秒内拒绝
  NTP 覆盖；无 BLE 校时可用时，Wi-Fi NTP 为备用，断开两种连接后仍本地走时。
  NTP 每小时轮询，不承诺在断开 BLE 第 120 秒立即切换来源。时区固定北京时间 `CST-8`。
- 使用 SDK 文档支持的 weak `sntp_sync_time` 替换，在写时钟前仲裁；不是先写后再回滚。
  依据：[ESP-IDF 6.0.2 / ESP32-P4 系统时间](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32p4/api-reference/system/system_time.html)。
  `pool.ntp.org` 为备用服务；网络/服务不可达时保持等待，不从天气时间或保存的时间伪造新校时。
- 新 `time_status` 诊断返回 valid/source/last_sync_age_ms/epoch_ms；不扩大原有资源诊断回复。
  PC 严格验证字段、来源一致性、启动 ID，数据错误使会话失败且不重发。
- 自动回归 **129 项通过**：新增 7 个实际 C 天气解析测试、4 个实际 C 时间优先级测试、
  3 个 PC 时间状态校验测试。完整回归使用日常 `.venv`（包含旧 serial 测试依赖）；
  BLE 常驻仍用独立 `.venv-ble`，没有因回归安装旧 USB 运行依赖。

### 增量实机复测（2026-10-02）

- 最终 B1 build/P4 flash/hash 校验通过，app **1,973,808 字节（0x1e1e30）**，SHA256：
  `B44F20C49F8663E8C430C11419FFE45DFB5711872CC9796D02433A10946C1F55`。
  只烧录 P4 bootloader/partition/app，未清除 NVS/绑定，未烧录 C6，未写 TF 文件。
- 暂停的是本轮独立 BLE launcher 20756 / worker 13104，操作前核验路径/父进程。
  旧用户日常 Agent 21400 保持运行，无 COM28 操作。有限观察器均已退出并关闭 COM27。
- 最终版本一次 UART RTS 重启后，未连接任何 BLE 客户端，NTP 在启动约 28.3 秒完成，
  HTTPS 天气在约 38.9 秒刷新；weather_configured/available=true、cached=false。
  55 秒观察没有 panic/复位循环。这是 UART 重启测试，**不是拔电后的冷启动验收**。
- 随后 Windows 既有绑定连接成功，MTU=256/ATT value=253；`time_status` 初始 valid=true、
  source=ntp，发送 BLE 时间后 source=ble，60 次音乐交换后仍为 ble。
  NTP 时间相对本机 PC 当时约 +4.028 秒，BLE 校时后读取约 -121 ms；PC 不是标准时钟，
  这些差值不等同于绝对校时精度验收。没有通过修改 PC 系统时间来测试。
- 真实 GSMTC/CoreAudio 经 BLE 的状态写入 **60/60**；交换中位 **227.6 ms**、p95 **346.7 ms**、
  max **400.4 ms**（不含 250 ms 间隔）。本次屏幕动作计数=0，封面 bytes=0。
  不将状态传输耗时当成用户按钮到实际播放器的动作耗时。
- 最终资源字节：internal_free=168695、internal_min=106187、host_stack_free=3256、
  tx_stack_free=2280。设备 Wi-Fi 在线、天气可用且非缓存；还不能代表五卡/宠物/长期内存预算通过。
- 同源天气解析迁移后的独立 B0 fallback 和旧日常固件均编译通过，未将它们烧录到设备。
  初次一次性验证脚本在完成交换后用了不存在的封面查询方法；已用实际 `cover()` 接口重新跑完整
  有限验证并正常退出。该脚本错误不记为设备故障，也不将首次退出记为完整验证通过。
- **待用户配合**：切到天气卡，确认中文排版、温度/图标/详细指标、更新时间和左右切换表现。
  网络中断缓存/恢复、双源均不可达冷启动、PC 休眠、长时间运行仍待单独验证。
- 已恢复独立后台 `start-ble-link.ps1 -Music`：launcher 16120 / worker 12480。
  15:39:45 完成 authenticated hello + time sync，15:40:15 同一连接 30 秒/58 次业务交换健康，
  本轮恢复后未见 OFFLINE。旧日常 Agent 21400 保持运行，COM27 未留观察器占用。

## B1 增量：严格按获批网页迁移视觉

用户指出原型缺少滑动、汉字、图标、两侧边缘，并明确要求严格按之前网页预览的样子。
`luna_preview_ui.c` 因此取代 B1 原 `luna_music_ui.c` 的编译入口；旧原型文件保留但不参与 B1。
视觉基准是 `docs/design/preview`，不是重新设计。竖向音量是用户明确追加的唯一外观变更，
网页 HTML/CSS 同步更新；数据文案可以依真实连接/可用性变化，不移植演示值。

- 720×720，主卡 590×450 位于 (65,135)，30×382 的左右边缘、顶栏、底部五图标导航；
  颜色/圆角/图标来自获批素材，切卡 230ms 平移/淡入/轻微缩放，支持左右手势。
- 滑动故障根因：LVGL 的 `GESTURE_BUBBLE` 连根节点也开启，手势继续冒泡至没有监听的 screen。
  在 UI root 截止手势，子对象仍冒泡。接受滑动后等待放手，避免顺带触发媒体按钮。
- 142×310 竖向音量 popup，滑动时不切卡；点击外部立即关闭，不吞掉原本按钮的动作。
- 字体原来只有小汉字子集；现在正文/歌名使用源字体提供的 31,031 字符集（16/28 px）。
  小号静态标签完整收录；地点用完整正文映射并缩放到预览字号。资源来源与 OFL 见 assets/README。
  字库不等同于覆盖全部 Unicode 或所有语言；Windows 网页字体与 Noto 字形不承诺逐像素相同。
- 云朵使用完整 160×105 位图，避免负 y 子对象被裁掉；未知天气代码保持 `?`，不伪造晴雨。
- 使用批准的月猫 atlas，不重新生图；连续轨道行走/休息、不瞬移、点击 1.6 秒响应。
  180 秒闲置进入三针表盘，首触摸只唤醒回原卡，后端刷新不唤醒；待机宠物睡觉/呼吸。
- Codex 两条剩余额度与 VS Code 工程、CPU/GPU/RAM/VRAM 外观已呈现；值明确未接入，
  空条旁显示不可用，不将未知显示为 0%。真实数据桥接仍是后续工作。

### 本机验证

- 网页 headless Edge 检查 54 项通过，包括新版竖向输入、滑动、外部关闭、闲置/唤醒与宠物。
  用户 IAB 页的自动读取发生超时，未据此声称目视验证了用户当前浏览器；源码和测试截图为基准。
- 新 `scripts/native-ui` 使用项目实际 LVGL 9.3 和实际 firmware UI，不模拟控件实现。
  实际指针滑动、快速双击、竖条拖动、外点关闭且播放、待机首触摸隔离、宠物响应全部通过；
  输出五卡/音量/待机绘图用于对照。还验证完整汉字/补充汉字实际 glyph 查询，以及 192 秒
  轨道逐毫秒连续性。此验证不代表物理触摸或屏幕观感验收。
- Python/C 完整回归 133 项通过（含 4 项资源哈希/覆盖/原分区地址保持检查）。
- 全字库使原 6 MiB app 装不下，B1 独立 `partitions.ble_b1.csv` 把 app 扩至 10 MiB，
  只使用已退役语音模型的空闲区；NVS 0x11000/0x6000、PHY 0x17000/0x1000、app 起点
  0x20000、storage 0xA20000/5MiB 均保持。B0/旧日常配置仍使用原 6 MiB 分区。
  没有格式化 TF，没有 erase-flash 或删除配对。

### 本轮烧录与有限硬件证据

- 首次移植 B1 app **9,713,408 字节（0x943700）**，10 MiB app 剩余 772,352 字节，SHA256：
  `C14D67897BB93313FF5A998608C24A0C4F167BF41F0E184C488A90E92E1D412A`。
  P4 bootloader/partition/app 烧录并 hash 校验通过；未烧录 C6、未擦 NVS/绑定、未写 storage/TF。
- 操作前核验 launcher 16120 的 executable/command line 与 worker 12480 的父进程/路径，
  只暂停这一部署的 BLE 树。旧日常 Agent 21400 始终保持运行，未操作 COM28。
- 烧录后一次 UART RTS 重启，55 秒只出现 1 个启动标记，未捕获 panic/assert/watchdog。
  触摸输入注册成功，32 MiB PSRAM 测试通过、剩余约 23 MiB 加入分配池。
  NTP 在约 32.2 秒成为有效来源，真实设备天气在约 41.1 秒刷新。观察器 finally 释放 COM27。
- 随后既有 Windows 绑定完成认证，ATT value=253，无重新自动配对。
  `time_status` 初始 source=ntp，BLE 校时后最终 source=ble；真实音乐/音量状态交换 **60/60**，
  median **281.0 ms**、p95 **344.0 ms**、max **531.0 ms**（不含 250 ms 间隔），屏幕动作计数=0。
  未自动播放/暂停/切歌/改音量。此数值不是物理按钮到播放器执行的耗时。
- 此次 internal_free=150139、internal_min=89747、host_stack_free=3244、tx_stack_free=2348 字节。
  wifi_online/weather_configured/weather_available=true、weather_cached=false。
  这只是有限运行观察，不替代 8 小时、反复滑动、PC 休眠或冷断电验收。
- 待物理屏幕验收：与获批网页对照五卡布局/边缘/图标/字体、左右手势、天气完整图形、
  竖向音量与外部关闭、小猫互动、3 分钟待机与首次唤醒。不可据本机绘图勾为已验收。

### 最后视觉微调

对照本机绘图再次校正电脑圆环的 133 px 外径/居中、内存条位置、导航圆角 18 px，
宠物呼吸改成预览规定的仅纵向缩放与 3.5 秒周期。实际 LVGL 的全部输入/状态断言和
资源/分区检查再次通过。无新增业务或变更字体/资源来源。

- 最终 app **9,713,552 字节（0x943790）**，app 剩余 772,208 字节，SHA256：
  `22073EE7F83E2207724A4CED245D9E2BDADA7069E85D4328CC88177FE0E00C01`。
- 核验并暂停本轮 launcher 19300 / worker 26880，只更新 P4 app（分区与 bootloader 已通过上次安装）。
  esptool 921600 传输完成并 hash 校验；擦写范围仅 0x20000–0x963fff，不覆盖 storage/NVS/C6/TF。
- 最终版本一次 UART RTS 重启，55 秒/1 启动标记，无捕获的 panic/assert/watchdog；
  NTP 约 28.6 秒有效，真实天气约 40.5 秒刷新，COM27 已释放。
- 最终版本既有绑定安全连接，ATT value=253，音乐/音量状态写入 60/60、屏幕动作计数=0；
  median 281.0 ms、p95 406.0 ms、max 469.0 ms（不含 250 ms 间隔）。有效时钟由 ntp 切到 ble。
  internal_free=150243、internal_min=88995、host_stack_free=3252、tx_stack_free=2284 字节；
  Wi-Fi 和设备天气可用，非缓存。探针有界结束并主动断开；没有自动发起播放或音量动作。
- 已恢复独立后台 launcher 24528 / worker 9680：17:01:05 完成认证/校时，17:02:06 同连接
  61 秒/106 次交换健康、未见 OFFLINE。旧日常 Agent 21400 保持运行。
  最后以 DTR/RTS=false 验证 COM27 可打开并立即关闭，没有请求复位，也没留观察器占用。
