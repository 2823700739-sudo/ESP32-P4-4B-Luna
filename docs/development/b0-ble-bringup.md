# B0：Luna 独立 BLE 诊断应用

维护证据：保留安全配对、旧 C6 HCI 兼容与诊断流程的实际记录。下方里程碑按当时状态记录，
不是当前功能/配置清单；当前状态以[current-status.md](current-status.md)为准。

日期：2026-10-02。U0 UI 已由用户确认，本轮开始无线链路实施。
本应用是短期诊断屏，不是五卡正式固件。**修正版已重新配对，常驻心跳与一次 P4 重启后的自动安全重连通过；当前后台保持连接，完整 B0 未完成。**
蓝牙广播名和 GAP Device Name 均为 **`Luna`**；不改为 Luna-B0 或追加设备序号。

## 已实现的路径

- 独立 `-BleB0` 构建：`build-ble-b0/`、`sdkconfig.ble_b0`，不改日常 `sdkconfig` 和 `build/`。
- P4 NimBLE host-only，经当前 ESP-Hosted 2.12.11 的 SDIO/VHCI 使用 C6 Controller。
- 读取 C6 固件版本，初始化/启用现有 Controller；不下载/重刷 C6。
  B0 专用 `LUNA_BLE_B0_LEGACY_HCI_PROBE` 默认仅在此诊断配置中打开：
  若版本 RPC 和 init RPC 均失败，尝试现有 HCI 的真实 NimBLE 同步。
  原因是本地锁定 Hosted 的 Bluetooth design 3.1 / CHANGELOG 2.5.2 明确说明：
  早于 2.5.2 的协处理器默认启用 Controller，缺少新版初始化 API。
  版本未知不等于确认旧版；必须收到 HCI 同步及实际广播后才可继续配对。
  已知版本的 init 失败或 enable 失败仍停止，不将错误当作成功，不降低安全条件。
- 设备屏幕显示数字比较码；用户核对 Windows 与 Luna 的数字后在两端确认。
  Luna 30 秒无确认拒绝；不支持数字比较的配对方法拒绝。
- Secure Connections、MITM、128-bit 密钥、绑定持久化；RX 要求认证/加密，TX 发送前再检查链路安全。
  单个活动连接，三个绑定存储槽；不使用自动驱逐绑定的 store callback，不自动删除旧绑定。
- hello 协商版本、启动 ID、会话 ID、最大消息；ping/pong 经过 TX 通知返回。
- 可显式 time_sync：传 UTC epoch_ms 和 PC 时区偏移，不重复加偏移；诊断屏固定北京时间 `CST-8`。
  生产版的 BLE/NTP 仲裁、跟随 PC 时区设置属于 B4，不冒充已经实现。
- 不启动 USB CDC、旧 HTTP 业务客户端、媒体操作、封面解码、TF 检查或 VS Code 桥接。

日常 Agent/COM28 没有被停止或重新配置；P4 当前暂时运行此诊断应用。

## 分片协议 v1

UUID：

| 用途 | UUID |
| --- | --- |
| Service | `c4a10001-9e7a-4b61-bb2b-15d21b8a4c01` |
| RX / write with response | `c4a10002-9e7a-4b61-bb2b-15d21b8a4c01` |
| TX / notify | `c4a10003-9e7a-4b61-bb2b-15d21b8a4c01` |
| READY / uncached read | `c4a10004-9e7a-4b61-bb2b-15d21b8a4c01` |

名字与 UUID 仅用于发现，不是认证凭据。Windows 同时匹配两个字段，多个候选时要求明确选择。
READY 为当前连接的 0/1 安全就绪标志，无业务数据/身份信息。客户端必须等到 1 后才订阅/握手；
板端 RX/TX 仍独立核验加密、认证、绑定与 16 字节密钥，不以客户端等待代替安全门槛。

每个 ATT value：12 字节 little-endian 头 + 非空片段。

| 字节 | 字段 |
| --- | --- |
| 0 / 1 | magic `0x4c` / framing version `1` |
| 2–3 | 非零 uint16 消息编号 |
| 4–5 | 片段在逻辑消息中的字节偏移 |
| 6–7 | 逻辑消息总长，1–4096 字节 |
| 8–11 | 整条逻辑消息的 CRC32（IEEE，Python zlib.crc32） |

偏移代替假设固定大小的片号；默认 MTU 23 时 ATT value 为 20 字节、片段数据为 8 字节。
初版收发刻意使用 20 字节。现已加协商：hello 本身仍用小分片，客户端提出实际 MTU-3 的有界上限，
设备取自身 MTU-3、客户端提议与协议 512 字节上限的最小值；后续双向发送使用此值。
缺少协商字段的旧客户端保持 20 字节，实际 MTU=23 时也保持 20；未知/异常 MTU 回退小包。
`--att-payload-cap 20` 可强制两方向小包路径，但这不是声称 Windows 实际协商 MTU=23。
每方向只重组一条消息，队列两条短诊断回复；收到下一条未完成消息时拒绝，不无限开缓冲。
支持乱序和重复片段；冲突重叠、越界、CRC 错误拒绝；30 秒绝对重组超时不会被重复片延长。
C 接收器约 4.6 KiB 固定存储；完整消息是 UTF-8 JSON，含 v/type/session。

断线或 Host reset 清理重组/握手，TX 带连接 generation，旧队列不能发入新连接。
B0 每连接只握手一次，请求编号单调递增；旧编号拒绝、不重放校时，编号耗尽必须重新连接。
Windows 单请求串行，写入+等待回复合计超时 8 秒；超时/断线后当前会话失效，不自动重发。
**完整消息的业务动作去重/真实结果缓存仍属 B1**，此协议层不替代它；B0 没有媒体动作。

## 构建与实测步骤

先构建（不烧录）：

```powershell
.\scripts\build-firmware.ps1 -BleB0 -Action reconfigure
.\scripts\build-firmware.ps1 -BleB0 -Action build
```

独立环境已经在本机创建。新机器可执行：

```powershell
.\pc-agent\.venv\Scripts\python.exe -m venv pc-agent/.venv-ble
.\pc-agent\.venv-ble\Scripts\python.exe -m pip install --index-url https://pypi.org/simple -r pc-agent/requirements-ble.txt
.\pc-agent\.venv-ble\Scripts\python.exe pc-agent/luna_ble_probe.py
```

默认只扫描，不配对、不打开串口、不访问旧 Agent。
Bleak 3.0.2 的 Python/Windows 条件已按 [PyPI 元数据](https://pypi.org/project/bleak/3.0.2/) 核对，
参数语义见 [官方客户端文档](https://bleak.readthedocs.io/en/latest/api/client.html)。
隔离安装避免改变当前 Agent 的 WinRT/串口依赖；本机 `pip check` 通过。

确认可暂时将 P4 的日常屏替换为诊断屏、且 UART 端口空闲后，才进行下一步：

```powershell
.\scripts\build-firmware.ps1 -BleB0 -Action flash -Port COM27
.\scripts\build-firmware.ps1 -BleB0 -Action monitor -Port COM27
.\pc-agent\.venv-ble\Scripts\python.exe pc-agent/luna_ble_probe.py --connect --time-sync
```

COM27 仅用于 P4 下载/日志与供电；它不承载诊断业务消息。Windows/Luna 配对数字必须由用户核对确认。
先在 Windows 设置 → 蓝牙和设备 → 添加设备 → 蓝牙中选择 Luna，核对两端数字并确认，
然后再运行 `--connect --time-sync`。探针使用 `pair=False`，不会自动代用户批准配对。
已检查本机 Bleak 3.0.2 WinRT 源码：其 `pair()` 仅支持 CONFIRM_ONLY 且自动 accept，
不适合此处的数字比较；不会通过降低 Luna 安全级别来兼容它。
关闭监视器后 COM27 要释放；COM28 不作为 B0 通道，不争抢旧 Agent 的句柄。
先用现有 C6 固件。若 Controller 不支持当前路径，应记录日志并停在此门槛，
先确认 C6 独立恢复方案，不运行未经核实的 C6 OTA/擦除。

需要回到已有日常固件时，可使用原 `build/luna_panel.bin` 对应日常 flash 流程；
单独构建 B0 不覆盖它，也不调用 `erase-flash`。
本轮在日常构建回归前还将原 app/bootloader/partition-table 三个 bin 留存在
`build-ble-b0/daily-before-b0/`，没有复制 Wi-Fi/Agent 配置或 token。
这些是构建镜像，不是完整 Flash/NVS 备份，不能声称已备份用户数据和绑定记录。

## 本轮证据与剩余门槛

- Windows 11 build 22631；Intel Bluetooth 驱动 21.80.0.3；Python 3.10.10。
- 首次 B0 ESP-IDF reconfigure/build 已通过；首版产物 `build-ble-b0/luna_panel.bin` 为 1,208,688 字节，
  SHA256 `E26F0F421EDE1E79B6C473FFE0533C18AAB762C2E66A7137FACF2278FAB62D86`。
  ELF 符号检查含 B0 `access_rx` / `app_main`，不含 `luna_usb_start`、
  `luna_agent_client_init`、`luna_weather_start`；依赖库存在不等于运行旧应用。
- 默认日常固件构建回归也通过，`CONFIG_BT_ENABLED` 仍未启用；没有将 B0 当作默认应用。
- 真实扫描成功执行、返回 0 个匹配 Luna 的广播；此时 P4 仍是未启用 BLE 的旧应用。
- 26 项新测试通过：17 项 framing/native-C parity + 9 项 probe/session；全部 Agent 测试共 78 项通过。
  其中 C/Python 完整 4 KiB、MTU23/大 MTU、乱序重复、跨计时器回绕、500 个坏包等被检查。
- 首次 IDF build 在 MAX_BONDS=1 的上游 NVS 排序代码遇到 GCC array-bounds 错误；
  改用三个存储槽，不关闭警告、不修改 SDK/managed component 源码。每个新绑定仍必须用户确认。
- CMake 早期依赖扫描不导入自定义 cache 开关：两条 source 分支均声明 bt 依赖，
  默认 CONFIG_BT_ENABLED 未启用时该组件不运行蓝牙；避免诊断源码找不到 NimBLE 头文件。
- 下文记录后续实机安全配对、订阅、收发、校时与一次连接重建结果。
  拒绝/超时、断电后的绑定持久化、异常断线恢复和 Wi-Fi 天气并发仍待测；C6 精确版本仍未知。
- 运行 RAM、任务高水位和 notify 吞吐待实机量测；编译成功不能证明这些指标达标。

### 2026-10-02 首次 P4 烧录 / C6 门槛

- 用户明确允许经 COM27 将 P4 临时切换为诊断屏。首版烧录通过，三段写入 hash 校验通过。
  只写 P4 bootloader / partition table / app，没有 erase-flash，没有覆盖 NVS，没有写 C6。
- 有限 UART 观察后释放 COM27；为完整抓取启动日志额外重启一次 P4。
  Hosted 启动自身会通过 GPIO54 重置 C6，这不是 C6 固件写入。
- 实机识别为 esp32c6，capabilities `0xd`，包含 `HCI over SDIO`；VHCI 驱动建立。
- `Req_GetCoprocessorFwVersion` (`0x15e`) 无响应；C6 精确版本未知。
  `Req_FeatureControl` (`0x183`，controller init) 无响应；首版按保守门槛停止。
  此时 Windows 实扫 0 个 Luna 广播，不能据此宣称硬件没有蓝牙。
- 接下来仅在 P4 B0 配置测试前述旧版 HCI 兼容路径；是否支持需以实际同步/广播验收，
  不据 RPC 失败自动决定 C6 OTA 或擦除。

### 兼容路径实测结果（同日）

- 兼容版再次 reconfigure/build/flash 通过，app 为 1,209,040 字节，SHA256：
  `7C2EC249AAE66C126EBEE825F8A2F2CA8C88978BA6EC50A27A3FBA9948FDF608`。
- 启动版本 RPC / controller init RPC 均返回 -1；进入明确标记的 HCI 探针。
  NimBLE 实际完成同步并启动广播，日志 `Luna: waiting for Windows BLE connection`。
- Windows 8 秒实扫匹配 1 个名称 Luna + 指定 service UUID 的设备，exit 0。
  证明现有 C6 HCI 路径可广播；精确 C6 版本仍未知，不能声称所有 RPC 兼容。
- 本次有限启动日志窗口未观察到 panic/assert/watchdog；不是长期稳定性验收。
  COM27 已关闭释放，未打开 COM28，未停止旧 Agent，未改写 C6 固件或擦除 NVS。
- Windows 探针已改为 `pair=False`；新增自动配对禁用的 mock 回归测试。
  当前 27 项 BLE 新测试 / 全部 79 项 Agent 测试通过；mock hello/ping 不计入实机结果。
- 下一门槛需要用户在 Windows 设置手动添加 Luna 并核对两端数字。
  尚未执行实机配对、GATT 收发、ping RTT、校时、绑定重连及 Wi-Fi 并发验收。

参考本地锁定依赖中的 `examples/host_nimble_bleprph_host_only_vhci`，
架构参见 [Espressif Bluetooth design](https://github.com/espressif/esp-hosted-mcu/blob/main/docs/bluetooth_design.md)。
未复制示例的固定 passkey、自动删除旧 bond 或 NVS 初始化失败即擦除的策略。

### 配对故障诊断（同日）

- Windows 改用高级设备发现后用户找到 Luna；用户报告两端显示相同数字，
  Luna 的 Numbers match 已点击，但 Windows 确认后提示重试。
- 旧诊断日志仅记录加密/认证未就绪并恢复广播，没有输出具体 status/reason；
  不能断言是 C6、Windows 驱动、用户确认超时或旧绑定导致。
- P4 B0 增加确认注入返回码/耗时、ENC status、安全位及断线 reason 的诊断日志，
  不记录比较数字、密钥或地址。屏幕底部保留确认/ENC/断线码，广播恢复不会覆盖。
  不降低 SC/MITM/128-bit/绑定要求，不删除绑定，不改 C6 或 managed component。
- 诊断增强版 build 已通过，app 0x12cd50 字节，SHA256：
  `1D90ACE861459A3A99ED8ED77691B27C9A95D8FE8B61F656B425D1F478FB24EE`。
- 首次尝试此版烧录失败：esptool 打开 COM27 时即失败，Win32 error 31
  “连接到系统上的设备没有发挥作用”。没有进入写入，也没有擦除。
  关闭本轮观察器后，再以 DTR/RTS=False 只读打开 COM27，仍返回相同 error 31。
  PnP 仍列出 CH343 COM27，Windows BLE 扫描仍发现 1 个 Luna；两者不代表 UART 可用。
- 用户重新插拔 USB-UART 后，COM27 恢复，增强诊断版烧录/校验成功。
  不改 C6、NVS；55 秒有限 UART 观察结束释放 COM27。
- 实机抓到 `PAIR_CONFIRM accepted=1 rc=0 elapsed_ms=7276`，另一次耗时 1611 ms，
  都未超时。随后 `BLE_ENC status=1035 (0x40b)`，encrypted/authenticated/bonded 全为 0；
  断线 `531 (0x213)`。0x40b = 本端 SMP DHKey Check Failed，0x213 = 对端终止连接。
  数字比较和 DHKey 校验不是同一阶段；确认正确不代表密钥计算已通过。
- 当前 SDK TinyCrypt P4 ECC 路径缺少上游相关修复，存在已报告的 DHKey 问题，
  参见 [官方问题 19002](https://github.com/espressif/esp-idf/issues/19002) 与
  [ECC 修复 9fd7cb7](https://github.com/espressif/esp-idf/commit/9fd7cb7)。
  此时仍是重点嫌疑，不以相同错误码单独断言根因，不重刷 C6 验证猜测。
- 使用 [IDF 6.0.2 官方配置](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32p4/api-reference/kconfig-reference.html#config-bt-nimble-crypto-stack-mbedtls)
  将独立 B0 的 SMP 与 NimBLE 密码库切为 mbedTLS，保留 SC/MITM/128-bit 与数字确认。
  仅修改项目配置，不改 SDK 源码或 managed component；日常 sdkconfig 未启用 BT。
  reconfigure/build 与 P4 烧录/hash 校验已通过，启动恢复广播。
  app 1,326,032 字节，SHA256：
  `273BF9EAE47839621323A1998398E5D662FDD140786052CFB716CD5F8C5257AF`。
  链接 map 含 NimBLE 使用的 PSA key API，不含 uECC_shared_secret；仅修改 B0 配置。
  不以编译/广播成功作为配对通过的证据，实际结果如下。

### mbedTLS 版实机安全配对与 GATT（同日）

- 用户核对 Windows/Luna 数字并在两端确认。设备记录
  `PAIR_CONFIRM accepted=1 rc=0 elapsed_ms=1959`，随后
  `BLE_ENC status=0 (0x0) find=0 encrypted=1 authenticated=1 bonded=1 key_size=16`。
  此版实际通过安全配对，之前的 DHKey Check Failed 没有再出现。
- Windows 首次配对后主动断连，随后 probe 能建立已认证的 GATT 会话。
  设备再次记录 ENC status=0、encrypted/authenticated/bonded=1、key_size=16。
  数字比较人工确认策略未改，probe 仍使用 `pair=False`。
- 第一轮真实 probe：匹配 1 个 Luna；hello 返回 `name=Luna`、`mode=b0-diagnostic`；
  实际 MTU=256，保守 ATT value=20 字节；通知返回 ping 10/10，
  RTT median=1257.5 ms、max=1329.0 ms；UTC time_sync 被设备接受，时区策略为 CST-8。
- 第一轮正常退出并断连后，再启动独立 probe；复用现有绑定，无需再次确认数字。
  hello、ping 10/10 与 time_sync 再次通过；RTT median=1265.0 ms、max=1313.0 ms。
  这是一次客户端正常退出后的连接重建，不等于异常断线、设备重启或 PC 休眠恢复验收。
- 两轮 probe 均 exit 0，未打开串口；有限 UART 观察器已关闭并释放 COM27。
  未打开 COM28、未停止日常 Agent、未修改 C6 固件或擦除 NVS。
- 当前往返约 1.3 秒，仅为保守小分片诊断基线，不满足直接声称媒体控制即时响应的条件。
  后续需基于实际 MTU 优化分片/发送节奏、保留最小 MTU 支持并做 p50/p95 测试。
  运行 RAM/任务栈、拒绝/超时、冷启动绑定、Wi-Fi 并发与长期稳定性尚未验收。
- 本轮全部 Agent 单元测试 79 项通过（其中 BLE 27 项）；mock 与上述物理结果分别记账。

### 后续重连问题与修正版（同日，尚待实机验收）

- 用户指出连接后又断开。核对 probe：它测试完成后主动退出 Bleak context 并断连，
  当时没有蓝牙常驻进程，旧 USB Agent 不会维持 B0 连接。不能将短时收发通过写成常驻在线。
- 尝试 60 秒保持连接的诊断时，hello 写入即返回 ATT 0x05 Insufficient Authentication；
  额外等待 3 秒仍失败，持续测试没有进入心跳阶段。先前一次重建不证明后续重连稳定。
- 原连接回调未主动启动安全流程。新增 `ble_gap_security_initiate()`，由 SDK 根据当前角色/绑定
  启动配对或加密；EALREADY 视为已在进行，其他启动错误阻断业务。
  客户端读取实时 READY（最长 45 秒）后才开始消息，GATT 服务发现也禁用 Windows 缓存。
  参见 [NimBLE 官方 GAP 文档](https://mynewt.apache.org/latest/network/ble_hs/ble_gap.html#c.ble_gap_security_initiate)。
- 首个修正版实测看到重复配对被拒绝、ENC status=7、安全位全 0，Windows 报操作取消。
  用户随后明确告知已在电脑删除 Luna：设备侧仍存在旧绑定，不能自动删除它来掩盖状态不一致。
- 新增仅在检测到未认证 peer 重复配对时显示的 `Forget old PC bond` 按钮（60 秒窗口）。
  按钮必须物理点击，经 NimBLE event queue 清理**此次请求对应**的旧 peer；
  generation/超时检查防止迟到点击，另一个 peer 正在连接时拒绝；成功认证后隐藏入口。
  不自动驱逐绑定、不清空全部绑定、不擦除 NVS。只记录操作/返回码，不记录地址/密钥/比较数字。
  清理后需要用户重新从 Windows 添加并核对两端数字；不会自动接受新的配对。
- 当前恢复按钮版 P4 build/flash/hash 校验通过，app 1,329,072 字节（0x1447b0），SHA256：
  `E063D5BD0C4BC7AAB9E8050FCD42F67CC7EC15B9ABDD5182FB82B3C268354552`。
  C6 与其他 NVS 配置未改；旧 Agent/COM28 未动。
- 新增独立 `luna_ble_link.py` 与 `start-ble-link.ps1`：不同于有限 probe，默认保持运行，
  10 秒心跳、60 秒校时、2–30 秒退避、断线新会话握手、不重放失败请求。
  独立 Windows 单实例 mutex，不使用旧 Agent 的 mutex；不打开 COM/HTTP/媒体业务。
  后台日志轮转上限三个 256 KiB 文件；未安装登录自启。当前尚未作为常驻进程部署。
- 当前 BLE 单元测试 38 项、全部 Agent 测试 90 项通过，PowerShell 启动脚本解析通过。
  用户报告已点击旧绑定清理按钮；本轮有限日志窗口未捕获清理返回码，仍需重配结果验证。
  **旧绑定清理、重新配对、连续保持连接、设备复位后的自动重连仍需实测**，不以单元测试替代。

### 恢复与常驻实测结果（同日，后续结果）

- 用户报告已点击旧绑定清理入口，重新从 Windows 添加 Luna。
  用户看到“已添加/配对”但未持续连接；不以 Windows 设置中的名称显示作为在线验收。
- 设备日志实际恢复 `ENC status=0`、encrypted/authenticated/bonded=1、key_size=16；
  新常驻链路通过 READY、hello、校时并开始心跳。没有再次出现认证不足。
  未捕获先前点击清理的返回码，因此只将按钮操作记为用户报告；重配后的安全通信独立有日志证据。
- 有限 `--run-seconds 120` 实测：包含扫描/认证阶段，日志记录连接持续 105 秒、10 次心跳回复，
  没有 OFFLINE；到指定 120 秒后程序记录 intentional disconnect，并正常 exit 0。
  此项证明程序运行时维持链路，不声称已完成长时间可靠性验收。
- 启动后台常驻版（不设 run-seconds），实际完成认证/hello/校时。
  Windows WinRT 从 BluetoothLEDevice 的真实 device_information ID 刷新后记录
  `is_paired=True`、`connection_status=CONNECTED`；find_all 返回的旧 DeviceInformation 属性不作为最终证据。
- 额外启动一次同入口，日志 `Luna BLE link is already running`，重复进程退出。
  最终只保留一个服务实例；venv pythonw launcher 8460 与其 worker 27060 为同一个进程树，
  不是两个活动 BLE 会话。旧 Agent 21400 不受影响。
- UART RTS 重启一次 P4（非断电测试）：后台记录 TimeoutError 后丢弃旧会话，不重放请求；
  14:02:01 OFFLINE，14:02:15 再次 CONNECTED，hello/校时重新成功。
  新启动日志再次确认 encrypted/authenticated/bonded=1、key_size=16，无需新的数字比较确认。
  证明本次 P4 重启后的绑定恢复和自动重连，不能替代冷断电、PC 休眠/蓝牙适配器关闭等场景。
- 当前后台继续运行，并未安装登录自启；本轮有限 UART 观察结束后释放 COM27。
  未打开 COM28、未改写 C6、未擦除全部 NVS、未停止旧日常 Agent。
- 剩余：异常恢复次数矩阵、拒绝/超时、冷断电、PC 休眠、Wi-Fi 并发、任务栈/RAM、
  长时间稳定性与媒体业务延迟；日常五卡固件/Agent 的业务迁移仍属后续阶段。

### MTU 延迟优化与资源量测（同日）

- 自适应版 build/P4 flash/hash 校验通过，app 1,330,160 字节（0x144bf0），SHA256：
  `F41141C2DFBB0F0459E952F304DEFF71988992D5D47B946ABE13612AC9315612`。
  不改 C6，不擦 NVS；暂时停止自己部署的 BLE 常驻进程树，旧 Agent/COM28 不动。
- hello 新增 ATT payload 提议/应答。探针初始 hello 小包，收到合法应答后才启用大包；
  布尔值、非整数、低于 20 或超出本端提议的应答拒绝，不按对端任意值扩容。
  TX 每片仍受当前连接 MTU 限制，不改变认证/绑定/CRC/超时与不重放规则。
- Windows 实际 MTU=256，协商 ATT value=253：真实 ping 30/30，
  中位 RTT=125.0 ms、p95/max=172.0 ms。
  强制双向 ATT value=20：ping 5/5，中位=1250.0 ms、p95/max=1328.0 ms。
  小诊断消息中位往返约降低一个数量级；这是传输结果，不是 Windows 音乐实际动作延迟。
- 同一真实链路发送精确 4096 字节的合成 JSON ping 10/10，
  中位=2281.0 ms、最大=2453.0 ms，低于当前 8 秒请求超时。
  大消息只验证完整 RX 重组与短 pong 返回；没有将其写成 4 KiB TX 吞吐或业务负载验收。
- 经认证 diagnostic 量测（字节）：内部 8-bit RAM 当前约 242491、最小 231999，
  NimBLE host 栈最小剩余 1964、TX 栈最小剩余 2408。
  高水位为启动以来最低剩余空间，不是已用值；IDF 返回字节，不再次乘 word 大小。
  见 [IDF FreeRTOS 文档](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32p4/api-reference/system/freertos_idf.html#_CPPv426uxTaskGetStackHighWaterMark12TaskHandle_t)。
- 此时尚未启用 Wi-Fi/HTTPS，以上不是并发内存预算。BLE 单元测试 44 项、全部 Agent 96 项通过。
- 开始可选 `LUNA_BLE_B0_WIFI_TEST` 共存门槛：复用设备天气模块、NVS 地点/缓存，
  从已存在的日常生成 sdkconfig 机械同步两项 Wi-Fi 设置到 Git 忽略的 BLE sdkconfig；
  不输出值，不迁移 Agent 地址/token。新开关默认关闭，只在本机诊断配置启用。
  新 profile 不将保存的天气时间戳当作 RTC；当前由 BLE 校时后再做 HTTPS，不启用 NTP 双源抢写。
  共存版单独结果见下节。

### Wi-Fi/HTTPS 与 BLE 共存实测（同日）

- 可选共存版 build/P4 flash/hash 校验通过，app 1,639,360 字节（0x1903c0），SHA256：
  `ED2DA9DFAD6EBB960D6392184F249A8132383368B649C1DD8BC828696C4CF455`。
  Wi-Fi 配置采用 RAM storage；未改写 C6，未清除 NVS/绑定，未启用 USB 或 PC HTTP。
- 烧录后曾出现 COM27/COM28 均不枚举、BLE 扫描无匹配；用户重新拔插后报告不再复位。
  串口与 BLE 随后恢复。未捕获那次异常启动日志，不能确定是供电、线缆还是固件原因。
- 恢复后做一次 UART RTS 重启，有限 55 秒日志观察无 panic/abort/复位循环。
  仍有旧 C6 version/init RPC 失败，但实际 HCI 同步、RAM-only Wi-Fi startup rc=0；
  Wi-Fi 两次离线重试后取得网络，设备 HTTPS 从 Open-Meteo 刷新天气，缓存标记变为 false。
- 同时真实 BLE 校时与 ping 80/80：MTU=256、ATT value=253，
  中位 RTT=125.0 ms、p95=188.0 ms、max=235.0 ms。之后继续保持 20 秒并读取诊断成功。
  encrypted/authenticated/bonded=1、key_size=16；无需新的数字确认。
- 最终诊断（字节）：internal_free=215499、internal_min=164051、
  host_stack_free=1592、tx_stack_free=2340；wifi_online/configured/available=true、cached=false。
  这只是首轮 HTTPS 并发资源门槛，不是长期内存或音乐动作/触摸响应验收。
- 最新软件回归 96 项通过。有限 BLE/UART 观察器均正常结束，COM27 已释放；
  本轮开发暂停的独立 BLE 常驻程序尚未恢复，旧 Agent 未停止。
  后续 B1 业务部署前需重新核对运行实例，不能把有限 probe 的正常退出当作异常掉线。
