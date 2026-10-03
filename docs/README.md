# Luna 文档入口

只维护当前无线产品路线：BLE 连接 Windows，Wi-Fi 独立天气与备用校时。

- [产品范围与开发计划](ideas/luna-desktop-companion.md)：功能边界和验收要求。
- [当前开发状态](development/current-status.md)：实际实现、验证及待完成项。
- [持续打磨与发布门槛](development/quality-roadmap.md)：优化顺序、产品边界和结束条件。
- [新会话交接](../HANDOFF.md)、[协作复盘](development/collaboration-retrospective.md)、[项目经验](development/project-experience.md)：每轮收尾与可复用方法。
- [Windows 安装与常驻](../pc-agent/README.md)：启动、停止、日志和排障。
- [设计基准与许可](design/ble-ui-references.md)、[预览说明](design/u0-ui-preview.md)、[交互预览](design/preview/index.html)。
- [性能与清理](development/ui-performance-20261002.md)、[常亮待机策略](development/standby-power-20261002.md)。
- [BLE 维护证据](development/b0-ble-bringup.md)、[B1 实机记录](development/b1-music-bringup.md)、[真实数据实现记录](development/dashboard-clock-20261002.md)。这些是技术证据，当前配置以当前开发状态为准。
- [TF 卡只读验证](development/tf-readonly-validation.md)、[固件素材与字体许可](../firmware/luna-panel/main/assets/README.md)。

旧 USB/HTTP、语音/麦克风、封面与旧卡片路线的 22 份文档已退出仓库。删除前的完整文档（含未提交内容、预览素材）与旧代码均在仓库外保留了本地恢复副本；这些副本不是构建输入或发布附件。当前性能/待机说明已整理，旧版保留在恢复副本中。

新增功能或配置必须同步产品计划与当前状态；历史成功、单元测试、构建通过都不能替代新版实机验收。
