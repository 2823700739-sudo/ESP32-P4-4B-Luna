# Luna 文档索引

面向使用者先读[项目说明](../README.md)，面向开发者读[开发文档](DEVELOPMENT.md)。当前产品为 Windows BLE + 设备 Wi-Fi，功能优化已暂停，发布缺口按实证保留。

## 使用与开发

- [Windows Agent 安装、常驻与排障](../pc-agent/README.md)。
- [网页交互预览](design/preview/index.html)：示例数据，不代表硬件验收。
- [原生 LVGL 验证工具](../scripts/native-ui/README.md)。
- [固件素材与字体许可](../firmware/luna-panel/main/assets/README.md)、[像素猫来源](design/preview/assets/README.md)。

## 维护与当前证据

- [根交接](../HANDOFF.md)、[当前状态](development/current-status.md)：配置、已部署范围、未验证项。
- [质量路线](development/quality-roadmap.md)：发布门槛与维护收尾规则，暂停不代表门槛通过。
- [部署与验证记录](development/deployment-20261003.md)：当前修复的实际部署证据。
- [协作复盘](development/collaboration-retrospective.md)、[项目经验](development/project-experience.md)：需求变化与可复用方法。
- [显示性能](development/ui-performance-20261002.md)、[待机策略](development/standby-power-20261002.md)：方案依据与验证边界。

## 保留的历史技术记录

- [B0 BLE 安全与 C6 兼容诊断](development/b0-ble-bringup.md)。
- [B1 音乐与硬件联调](development/b1-music-bringup.md)。
- [TF 只读验证](development/tf-readonly-validation.md)。

以上历史记录用于排障和追溯；其中旧地址、命令、镜像和中间状态不能作为当前部署配置。实际配置以根交接、当前状态和源代码为准。

已删除被实现取代的产品草案、UI 参考草案、U0 预览迭代说明与中间数据集成记录；当前内容收拢到项目说明和开发文档。删除前完整文档恢复副本保存在仓库外，仅供维护者本机恢复，不是公开构建依赖。独有的安全兼容、实机证据、素材来源与许可记录继续保留。
