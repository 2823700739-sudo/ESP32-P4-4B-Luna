# Luna · A desktop companion under the stars

[中文](README.md) · **English**

Luna is a wireless desktop companion built around ESP32-P4. Five swipeable cards on a 720×720 touchscreen bring together time, music, weather, Codex quota and PC metrics. A starfield and an interactive pixel cat add a little companionship to your desk.

Authenticated, encrypted BLE connects Luna to Windows. Device-owned Wi-Fi handles weather and backup time synchronization. The DC-powered display stays on, with unchanged brightness in standby.

The **2026.10.08 public preview** includes source code, deployment instructions and binaries without personal Wi-Fi credentials. All illustrations below come from the existing website preview with demo data; they are not hardware photographs. The device UI currently uses Chinese; this English introduction does not imply an on-device language selector.

## Screens and features

### Clock · Time at a glance

The default digital clock shows the date and time. BLE time synchronization takes priority, with NTP available as a network fallback. The current timezone is Beijing time (UTC+8).

<img src="docs/images/clock.png" width="360" alt="Luna digital clock with stars and a pixel cat">

### Music · Controls within reach

View the active media session's title, artist and playback state. Play, pause, previous and next use Windows GSMTC; the player must expose a supported system media session.

<img src="docs/images/music.png" width="360" alt="Luna music card with a record and playback controls">

### Volume · Windows system audio

The right-side volume button opens a vertical slider with mute at the top. Tap outside to close it. Playback and volume commands travel over secure BLE; uncertain commands are never replayed after reconnection.

<img src="docs/images/music-volume.png" width="360" alt="Luna vertical volume slider and mute popup">

### Weather · Fetched by the device

The device retrieves weather over Wi-Fi and HTTPS and retains a local cache. The Windows Agent does not forward weather. **The public binary has no Wi-Fi credentials, and an initial location setup entry point is not yet available.** Devices with an existing saved location require a locally configured build for networking. A blank device cannot currently enable weather directly. This illustration shows demo weather.

<img src="docs/images/weather.png" width="360" alt="Luna weather card showing demo conditions and measurements">

### Codex and workspace · Your work at a glance

See remaining five-hour and weekly quota alongside the most recent foreground VS Code workspace name. Quota comes from the local Codex App Server; the workspace comes from its window title. Missing sources stay unknown, and source files and full paths are not transmitted.

<img src="docs/images/codex.png" width="360" alt="Luna Codex quota bars and VS Code workspace card">

### PC metrics · The essentials

Two rings show CPU and GPU usage, with bars for RAM and dedicated VRAM. The Windows Agent reads system APIs; missing readings stay unknown. Temperatures are unavailable, and no additional monitoring driver is required.

<img src="docs/images/computer.png" width="360" alt="Luna CPU GPU rings and RAM VRAM bars">

### Standby and the pixel cat · Quiet company

After three minutes without input, or a tap on the available blank area above the card, Luna switches to an analog clock, sleeping cat, floating ZZZ and a warm sentence. The first touch only wakes the previous card. In normal use, the cat blinks, wanders and responds to taps.

<img src="docs/images/standby.png" width="360" alt="Luna analog standby clock and sleeping pixel cat">

## Hardware and technology

| Item | Supported baseline |
| --- | --- |
| Board | Waveshare ESP32-P4-WIFI6-Touch-LCD-4B, 720×720; this image targets P4 rev1.x and retains the existing onboard C6 controller |
| Power and display | Always-on DC power; 360 MHz CPU, DOUBLE_DIRECT double buffering, no automatic standby dimming |
| Firmware | C / FreeRTOS, ESP-IDF 6.0.2, LVGL 9.3.0, ESP-Hosted 2.12.11 |
| PC | Windows 11 (build ≥ 22000), Bluetooth adapter, Python 3.10 |
| Data interfaces | GSMTC, Core Audio, Win32, DXGI / PDH, local Codex App Server, Open-Meteo HTTPS |

The product focuses on Windows BLE and device Wi-Fi. There is no macOS/Linux Agent, voice, microphone, album-cover or USB OTG product feature. UI assets are built in rather than loaded from TF storage.

## Download and deploy

1. Read the [deployment guide](docs/DEPLOYMENT.en.md) and check your board, silicon revision and existing partition layout.
2. Download the app, bootloader and partition-table binaries from the [public firmware directory](firmware/releases/2026.10.08-preview/README.md), then verify SHA-256.
3. Follow the appropriate existing-device upgrade or first-install procedure, pair Luna in Windows, and install the BLE Agent.

[App binary](firmware/releases/2026.10.08-preview/luna-panel.bin) · [Bootloader](firmware/releases/2026.10.08-preview/bootloader.bin) · [Partition table](firmware/releases/2026.10.08-preview/partition-table.bin) · [Checksums](firmware/releases/2026.10.08-preview/SHA256SUMS.txt)

[Download this version as a source + binaries ZIP](https://github.com/2823700739-sudo/ESP32-P4-4B-Luna/archive/refs/tags/v2026.10.08-preview.zip). The source is also available through GitHub **Code → Download ZIP**, or by cloning the current product branch:

```powershell
git clone --branch codex/usb-r2 https://github.com/2823700739-sudo/ESP32-P4-4B-Luna.git
```

## Source and documentation

```text
firmware/luna-panel/                 ESP-IDF firmware and built-in assets
firmware/releases/2026.10.08-preview/ Credential-free binaries and manifest
pc-agent/                           Windows BLE Agent
scripts/                            Build, preview and maintenance tools
docs/design/preview/                Existing interactive website preview
docs/images/                        Product illustrations
```

- [Deployment guide](docs/DEPLOYMENT.en.md) / [部署教程](docs/DEPLOYMENT.md): flashing, pairing, setup, builds and troubleshooting.
- [Development guide (Chinese)](docs/DEVELOPMENT.md): architecture, modules and protocol.
- [Documentation index](docs/README.md): public documents and asset provenance.

This version excludes local credentials, runtime logs, virtual environments, toolchains, device NVS/bonds/TF data and internal test journals. Regression test source and CI remain available for maintenance.

## Current limitations and licensing

Initial weather location setup is missing, Wi-Fi uses local build configuration, and the timezone is fixed to UTC+8. The Codex quota source may temporarily fail. Customized VS Code titles and players without GSMTC support may be unavailable. These credential-free preview binaries have not completed first-install and long-duration acceptance on a fresh device.

A repository-wide license has not been selected; the project does not claim MIT or Apache licensing. Noto Sans CJK includes [SIL Open Font License 1.1](firmware/luna-panel/main/assets/OFL-NotoSansCJK.txt), and dependencies retain their own licenses. [Firmware assets](firmware/luna-panel/main/assets/README.md) and [pixel-cat provenance](docs/design/preview/assets/README.md) document their sources and conversion.
