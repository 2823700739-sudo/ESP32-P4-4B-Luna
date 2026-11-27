# Luna 2026.11.27 定版 / Release

本版本由用户确认定版，源代码位于主分支 `main`，固定标签 `v2026.11.27`。分发版本为 **2026.11.27**；应用内版本 **2026.11.27-focus-align**，四份 bin 与已更新设备的冻结版本完全相同，未为改名重新编译。

- 六卡：时钟、音乐、天气、Codex 额度、电脑状态、专注。
- 专注：25/45/60 或自定义1–999分钟，绿色K2数字键盘，全屏模态阻挡底层，运行禁止时钟待机、暂停允许；仅原行走猫提醒。文字按可见字形上下左右居中；计时重启重置。
- 手机配网：逐行Wi-Fi列表、连接/保存就近状态，设备弹窗阻挡底层，暂时隐藏后热点继续。
- 音乐：加长音量条、放大下移按钮与播放落针/暂停停靠。

已有相同B1布局的Luna：`luna-panel.bin` 写 **0x20000**，保留配对/NVS。首次安装：`luna-first-install-0x0.bin` 写 **0x0**，会清NVS、配对、网络和地点/缓存。二选一，不整片擦除，不刷C6。仅P4 rev1.x / 32MB及兼容现有C6。

先核对 `SHA256SUMS.txt`，按[快速部署](../../../docs/HUB-QUICKSTART.md)或[完整部署教程](../../../docs/DEPLOYMENT.md)操作。手机设置见[中文](../../../docs/PHONE-SETUP.md)/[English](../../../docs/PHONE-SETUP.en.md)，专注见[说明](../../../docs/FOCUS.md)。

User-approved release. Distribution version **2026.11.27**, embedded app version **2026.11.27-focus-align**. The binaries are byte-identical to the deployed candidate. Use app **0x20000** for a matching existing Luna, or merged **0x0** for first installation with NVS/bonds/settings cleared. P4 rev1.x /32MB and compatible existing C6 only. Full-duration timing, new merged first-install and newly saved setup power-cycle/long-run checks were not individually recorded; see `manifest.json`.

分发必须保留 [LICENSE](../../../LICENSE)、[NOTICE](../../../NOTICE)、[第三方说明](../../../THIRD_PARTY_NOTICES.md)及完整[LICENSES](../../../LICENSES/)目录。/ Retain all project and upstream license materials.
