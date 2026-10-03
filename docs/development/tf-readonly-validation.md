# TF 卡只读实机验证

日期：2026-10-02。用户已插卡并明确允许烧录临时测试固件。

## 实测结果

板卡：ESP32-P4-86-Panel-ETH-2RO，P4 rev1.3；ESP-IDF 6.0.2。
使用 COM27 烧录/观察；未打开或抢占 COM28，原 Windows Agent 保持运行。

| 检查 | 实测 |
| --- | --- |
| 卡识别 | `SD64G`，60906 MiB（约 59.48 GiB，标称 64 GB 级别） |
| 文件系统 | 挂载成功；509 MiB，空闲 424 MiB |
| 根目录抽样 | 扫描 32 项上限；读取 3 个现有文件，共 1169 字节；不输出文件名或内容 |
| 扇区读取 | 首次读取扇区 0 后重复 20 次；20/20 成功且内容一致 |
| Wi-Fi | 扇区检查结束时 `wifi_online=1`；随后 Agent 诊断仍为在线 |
| 电脑连接 | 原 Agent 重新接入 COM28，设备诊断未过期、连续通信错误为 0 |
| 写入保护策略 | 挂载失败不格式化；仅 `rb` 打开现有文件；不创建、写入或删除卡上文件 |
| UART 观察 | 60 秒内观察到 PASS，未观察到崩溃标记；结束自动关闭 COM27 |

测试固件 ELF SHA256：
`8c83969c19b16b7b5753eacf2a2b14891dc884a6bfe41acca413ce713ecaf2f4`。
Agent 回报的运行固件哈希与本地 ELF 一致；观察时 uptime 为 55 秒。
随后 uptime 增至 216 秒，boot ID 保持不变、Wi-Fi 仍在线，
Agent 累计错误数仍为 176、连续错误为 0；这只是短时检查，并非长期稳定性验收。
累计 Agent 通信错误为历史计数，不应当作本次测试新发生的错误或清零后结果。

关键原始日志（已限制为不含文件名/内容和凭据的诊断行）：

```text
TF_TEST begin: read-only, format=never, file_writes=0, slot=0
TF_TEST mounted: card=SD64G capacity_mib=60906 filesystem_mib=509 free_mib=424
TF_TEST directory: scanned=32 limit=32 files_read=3 bytes_read=1169 (no names/content logged)
TF_TEST PASS: repeated_reads=20/20 wifi_online=1 file_writes=0
Result: observed_pass=True, observed_failure=False
Released COM27.
```

## 范围与限制

- 结论是本次插入卡的识别、挂载与有限只读操作可用，不是读写性能、全容量、坏块或长期寿命认证。
- 卡容量与当前挂载文件系统容量不同，可能与既有分区/格式有关；未检查全部分区，不能据此判定卡损坏。
- 未修改分区、未格式化、未删改卡上文件，也不自动尝试“修复”文件系统。
- Wi-Fi 在线是检查结束及后续诊断采样结果，不代表整个过程吞吐无波动；尚未验证 TF + Wi-Fi + BLE 并发。
- 这次没有改版 UI、部署 TF 宠物资源包或验证触摸；原 USB/HTTP 业务仍存在。

## 实现与复测

TF 是 SDMMC slot 0，C6 Hosted 是 slot 1；它们共享同一主控制器，
不能把两个 slot 当作两个独立控制器。测试在 Hosted 初始化后复用控制器，
不再次调用 `sdmmc_host_init()`，失败清理只释放 TF slot。
挂载明确设置 `format_if_mount_failed=false`，使用保守的默认 SDMMC 频率。

`LUNA_ENABLE_TF_TEST` 默认为关闭。仅有意进行诊断时开启，在 reconfigure 后构建，
通过 COM27 烧录；测试在无线初始化后等待 10 秒，便于连接只读观察脚本：

```powershell
.\scripts\build-firmware.ps1 -Action reconfigure
.\scripts\build-firmware.ps1 -Action flash -Port COM27
.\pc-agent\.venv\Scripts\python.exe .\scripts\observe-tf-readonly.py --port COM27 --seconds 60
```

脚本不发送命令、不控制复位，限制观察时长且在结束/异常时关闭串口。
复测完成后关闭配置，再 reconfigure、构建并烧录，避免日常启动重复测试。
不要使用 `erase-flash`，不要抢占原 Agent 的 COM28。

本次代码验证：固件构建通过；Agent 回归与观察器模拟测试共 52 项通过。
模拟测试只验证观察器过滤、失败判定与端口关闭，不代替上述实机结果。

## 日常固件恢复

实测后已将本地 `sdkconfig` 的 `LUNA_ENABLE_TF_TEST` 关闭，重新配置、
构建并通过 COM27 烧录成功。当前常规启动不挂载 TF，也不执行开机卡测试。
只读诊断代码保留，供后续按需复测，不代表资源加载器已启用。

恢复固件 ELF SHA256：
`1d51771e6cbd6f7641eb354698ed49083c75fe4f3bf5d3eaaa12ceddeee78670`。
Agent 回报运行哈希一致，启动第 10 秒 Wi-Fi 尚未在线，第 20–50 秒采样均在线，
boot ID 不变、累计错误计数保持 211、连续错误为 0；电脑连接正常、设备状态未过期。
实际释放后又短暂打开/关闭 COM27，
确认串口可用且未发送命令或复位。COM28 仍由用户原 Agent 正常使用，未抢占或关闭。

## 下一步用途

优先作为可选的像素宠物精灵图与主题素材存储。资源加载/校验/缺卡回退尚未实现，
需要在 UI 预览确认后落地；加载到内存后播放动画，不在每帧同步读取卡。
现有可用空间已足够做小型素材验证，不需要为了这一步更改现有分区。
