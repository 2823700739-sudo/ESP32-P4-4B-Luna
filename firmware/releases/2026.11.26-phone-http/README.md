# Luna 手机配网候选 / Phone setup candidate

2026.11.26-phone-http 分支预览版，源代码位于 `codex/phone-wifi-setup`。P4 app 写入/校验完成，作者已确认 Wi-Fi 实测可用；天气地点保存、新合并首装、断电恢复与长期运行尚未逐项确认，详见 manifest.json。

- 已有同布局 Luna：app 写 **0x20000**，保留配对/NVS；合并 **0x0** 仅允许清数据首装，会清网络/配对/地点/缓存。不刷 C6、不整片擦除。
- P4 rev1.x/32MB/DIO80m，360MHz/DOUBLE_DIRECT 保持。需要现有 C6 支持 Hosted Wi-Fi；若初始化失败，不宣称已可手机配网。
- 新版默认手机设置：天气卡 Wi-Fi → 连接屏幕的临时无密码热点 → http://192.168.4.1 → 扫描并选择2.4GHz网络、输入密码试连、选城市并保存。电脑脚本只用于电脑功能；BLE配置保留可选入口。
- 公共 bin 无个人网络配置。设备 NVS 没有静态加密，勿传播设备 Flash 镜像。

[中文教程](../../../docs/PHONE-SETUP.md) / [English](../../../docs/PHONE-SETUP.en.md)。分发保留 [LICENSE](../../../LICENSE)、[NOTICE](../../../NOTICE)、[第三方说明](../../../THIRD_PARTY_NOTICES.md) 和 [LICENSES](../../../LICENSES/)。
