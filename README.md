# Luna desktop companion

Luna is a desktop card display and touch controller for the Waveshare
ESP32-P4-86-Panel-ETH-2RO (SKU 31570). The target board has ESP32-P4 silicon
revision 1.3 and is powered over USB-C. The current firmware uses native USB
OTG as the primary PC link, with Wi-Fi for independent network services and
fallback connectivity. See the
[product restart plan](docs/ideas/luna-desktop-companion.md).

The project is split into:

- `firmware/luna-panel`: ESP-IDF firmware for the panel.
- `pc-agent`: Windows companion service for panel state and media controls.
- `docs/ideas`: product scope and feasibility decisions.
- `docs/development`: repeatable build, flash, and hardware validation notes.

The firmware baseline is the existing ESP-IDF v6.0.2 checkout at
`D:\.espressif\v6.0.2\esp-idf`.

## Current milestone

The active implementation milestone is **R5 stability and diagnostics**. PC
state and media actions are already available over USB CDC, and R3 enforces a
single active PC transport. Hardware acceptance for USB covers, repeated
reconnects, and Wi-Fi fallback remains open. See
[R3 双通道切换验证](docs/development/r3-dual-transport.md).
Device-owned weather has passed an Agent-offline reboot smoke test; see
[R4 面板独立联网天气](docs/development/r4-device-weather.md).
The first R5 changes add persistent Wi-Fi recovery and runtime diagnostics;
see [R5 稳定性与诊断](docs/development/r5-stability.md).
The diagnostics screen also tracks USB opens, handshakes, exchanges, and errors
for reconnection testing.
Runtime diagnostics are also sent over USB to the Agent for device reboot and
memory records; see [USB 设备运行状态同步](docs/development/r5-device-diagnostics.md).
Codex and music cards now mark last-known PC data when the PC link is offline;
music and volume controls remain disabled until the link recovers.
The Windows Agent now supports per-user login auto-start, so USB OTG plug-in
can reconnect without manually running a script after each login; see
[Windows 助手启动说明](pc-agent/README.md).

Implementation and hardware acceptance steps are in
[R1 USB P0 实机验证](docs/development/r1-usb-p0.md).
USB state and action migration is tracked in
[R2 USB 状态与动作验证](docs/development/r2-usb-state-actions.md).

Voice control is paused. The firmware no longer loads ESP-SR speech models,
listens for a wake word, or executes spoken commands. The microphone level
meter remains on the hardware diagnostics screen. Any future AI assistant is
a separate design decision.

P1 provides the desktop card experience:
a four-card horizontal carousel: a combined Codex quota and current-project
workspace, music, weather, and a standalone clock with a built-in background.
The firmware keeps only the current card and its two neighbours instantiated. The
panel connects to a small Windows companion over the local network. The music
card reads the real NetEase Cloud Music title, artist, cover and playback state
through Windows GSMTC, and can send previous, play/pause, and next commands.
The Codex workspace card reads the signed-in account's live primary and weekly
rate-limit windows through the local Codex App Server, and identifies the most
recent project from thread workspaces. The weather card uses configured
coordinates to show live Open-Meteo conditions and explicitly marks cached data
during an outage. The original hardware checks remain available from the home
screen. The clock synchronizes over Wi-Fi and keeps running while the Windows
companion is unavailable, as long as the panel remains powered.

Start with the current [590×450 音乐控制卡实机验证](docs/development/p1-music-controls-590x450.md).
For the clock and four-card carousel, use
[壁纸时钟卡实机验证](docs/development/p1-clock-card.md).
For the no-voice regression checks, use
[无语音版本实机验证](docs/development/no-voice-validation.md).
For carousel and long-run checks, use
[P1 stability and carousel validation](docs/development/p1-stability-carousel.md).
For the latest swipe animation and black-area check, use
[卡片滑动与果冻回弹实机验证](docs/development/p1-jelly-carousel.md).
For the initial connection checks, use
[P1 card and Windows-agent validation](docs/development/p1-card-agent.md).
For the latest music metadata and cover checks, use
[P1 music card validation](docs/development/p1-music-card.md).
For live Codex quota and project checks, use
[P1 Codex card validation](docs/development/p1-codex-card.md).
For configured coordinates, live weather, and cache checks, use
[P1 weather card validation](docs/development/p1-weather-card.md).
Use [P0 bring-up](docs/development/p0-bringup.md) when diagnosing the board.

## Documentation language

面向使用者的实机验证、操作步骤、测试记录和故障排查文档统一使用中文；
命令、接口路径、配置项名称和原始日志按实际内容保留。
