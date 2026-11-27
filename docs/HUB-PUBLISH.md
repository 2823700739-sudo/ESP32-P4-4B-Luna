# 微雪固件广场发布材料 · 2026.11.27

适用产品：**ESP32-P4-WIFI6-Touch-LCD-4B**。GitHub [定版下载](https://github.com/2823700739-sudo/ESP32-P4-4B-Luna/releases/tag/v2026.11.27)与[源码](https://github.com/2823700739-sudo/ESP32-P4-4B-Luna)。微雪[固件社区入口](https://hub.waveshare.net/)的实际表单、大小及格式限制以上传页面为准。

## 建议填写内容

**标题：** Luna：繁星与像素猫的无线桌面伙伴

**版本：** 2026.11.27（固件应用内显示 2026.11.27-focus-align）

**简介：** Luna 将 720×720 触摸屏变成桌面伙伴，提供时钟、音乐、独立天气、Codex 额度与工程、电脑状态、专注计时六张卡片。繁星背景配合可互动的行走像素猫；手机连接设备热点即可设置 Wi-Fi 和天气地点。专注支持 25／45／60 分钟及绿色数字键盘自定义 1–999 分钟，到时由小猫提醒。音乐支持播放、暂停、切歌、系统音量与唱针动画。专注运行时保持唤醒，暂停后允许时钟待机。

时钟、天气和专注独立运行；音乐、电脑指标、工程与 Codex 额度需要 Windows 11 x64、蓝牙和 Python 3.11 x64，配对后双击 Start-Luna.cmd。额度需要已有可用 Codex 环境，媒体控制取决于播放器的系统媒体会话支持。

**安装说明：** P4 rev1.x / 32 MB，沿用板载兼容 C6。首次安装将 luna-first-install-0x0.bin 写入 0x0（DIO / 80 MHz / 32 MB），将清除原有配置与配对。已有匹配 Luna 分区时，保留数据升级只将 luna-panel.bin 写入 0x20000。烧录前核对 SHA-256。手机连 Luna 热点，访问 http://192.168.4.1，扫描列表点选 2.4 GHz 网络并连接，选择地点后保存，分别确认连接和保存结果。详见[快速部署](HUB-QUICKSTART.md)和[完整部署](DEPLOYMENT.md)。

**许可：** 项目 Apache-2.0；第三方组件及字体各自许可随完整许可包提供。分发固件和 Windows 包时同时保留 LICENSE、NOTICE、THIRD_PARTY_NOTICES.md 和 LICENSES 全目录。

**已知范围：** 当前中文界面、北京时间、DC 常亮；专注状态重启后重置。未包含 C6 固件。其他板型/芯片版本不作为适配承诺。用户已整体确认定版，完整计时、新合并首装、新配置断电恢复及长时稳定性未逐项记录；详见[发布清单](../firmware/releases/2026.11.27/manifest.json)。

## 附件与图片

| 文件 | 用途 |
| --- | --- |
| luna-first-install-0x0.bin | 首装合并镜像，写入 0x0 |
| luna-panel.bin | 匹配分区的 app-only 升级，写入 0x20000 |
| Luna-Windows-2026.11.27.zip | 用户双击启动的完整 Windows 源码运行包 |
| Luna-Source-Firmware-2026.11.27.zip | 完整公开源码、固件与许可 |
| Luna-Licenses-2026.11.27.zip | 单独分发 bin 时附带的全部许可 |
| Luna-Hub-Images-2026.11.27.zip | 1920×1080 封面及九张功能图 |
| SHA256SUMS.txt | 上传附件校验值 |

图片来自实际项目网页预览，使用示例数据，**不是实机照片**。首图 `00-cover.png`，包含专注与绿色键盘的 `07-focus.png` / `08-focus-keyboard.png`。对应[图片清单](images/hub-16x9/manifest.json)。若上传页裁剪封面，先检查预览中产品与文字均完整。

上传时填标题、版本、硬件、简介与安装说明，附固件、运行包、源码和许可，再选择封面与功能图。提交前在页面复核附件名称、烧录地址和图片裁剪。公开 GitHub Release 不等于已向固件广场提交。
