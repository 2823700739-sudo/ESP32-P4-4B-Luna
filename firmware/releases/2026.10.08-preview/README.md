# Luna 2026.10.08-preview · Public firmware

[中文部署教程](../../../docs/DEPLOYMENT.md) · [English deployment guide](../../../docs/DEPLOYMENT.en.md)

本目录提供无个人 Wi-Fi 凭据的 P4 B1 公开预览镜像，适用于 Waveshare ESP32-P4-WIFI6-Touch-LCD-4B / P4 rev1.x / 720×720 / 32 MB Flash。沿用现有板载 C6，不提供 C6、NVS、绑定或 TF 镜像。公开构建尚未完成全新设备与长期运行验收。

Credential-free P4 B1 preview for the board above. It retains the existing C6 firmware and includes no NVS, bonds or TF data. Fresh-device and long-duration acceptance remain incomplete.

| Download | Purpose / 用途 | Offset |
| --- | --- | --- |
| [luna-panel.bin](luna-panel.bin) | App-only upgrade / 已有匹配 Luna 的正常升级 | `0x20000` |
| [bootloader.bin](bootloader.bin) | First-install P4 bootloader / 首次安装 | `0x2000` |
| [partition-table.bin](partition-table.bin) | First-install 10 MiB app layout / 首次安装分区表 | `0x10000` |
| [manifest.json](manifest.json) | Version, compatibility, offsets, sizes and hashes / 发布清单 | — |
| [SHA256SUMS.txt](SHA256SUMS.txt) | Check before flashing / 刷写前校验 | — |

**已有设备只更新 app，不要为了升级写 bootloader 或分区表。** 首次安装必须先确认兼容布局；不要整片擦除，不用 force，不刷 C6。具体命令见部署教程。

**Existing devices use app-only updates.** First installation requires layout compatibility checks. Do not erase the chip, bypass checks or update C6. Follow the deployment guide.

公开镜像的 Wi-Fi SSID / 密码为空，天气图是网站示例。联网天气需要私人构建与已有 NVS 地点；首次地点配置入口目前缺失。不要上传含自己 Wi-Fi 凭据的镜像。

The Wi-Fi SSID/password are empty. Network weather needs a private build and an existing saved location; initial location setup is missing. Keep credential-bearing private builds off GitHub.

完整源代码包含在[仓库](../../../README.md)中，依赖版本由组件锁文件与 PC requirements 清单管理。第三方资源和字体继续适用各自许可。

[Repository source](../../../README.en.md) includes firmware, Agent, configuration defaults and build tools. Dependencies and fonts retain their own licenses.

许可材料更新版 **2026.10.08-preview.1** 沿用本目录三份 bin，其内置应用版本仍为 `2026.10.08-preview`，大小和 SHA-256 不变。自有代码采用 Apache-2.0；单独分发 bin 时也须附带 [LICENSE](../../../LICENSE)、[NOTICE](../../../NOTICE)、[中英第三方许可说明](../../../THIRD_PARTY_NOTICES.md)和完整 [LICENSES/](../../../LICENSES/) 目录。推荐[完整源码、bin 与许可 ZIP](https://github.com/2823700739-sudo/ESP32-P4-4B-Luna/archive/refs/tags/v2026.10.08-preview.1.zip)。

Distribution **2026.10.08-preview.1** adds license materials; all three binaries, embedded app version, sizes and SHA-256 values remain unchanged. Luna-owned code uses Apache-2.0. Separate binary distributions must include the license materials linked above; the complete ZIP includes them together.
