# Luna desktop companion

Luna runs on the ESP32-P4-4B touch panel. The current product uses **BLE for PC control/state**, **Wi-Fi for device-owned weather and backup NTP time**. The user's device is DC-powered and always on; UART is only for development/flashing. No USB OTG application, microphone, speech recognition or album-cover pipeline is included.

## Current scope

- Approved moon-night/starfield UI: five swipeable cards, side edges, default digital clock and interactive pixel cat.
- Real BLE music controls and Windows system volume; one right-side volume button opens a vertical slider with mute at its top.
- Independent Wi-Fi weather and BLE-first clock.
- Live Codex five-hour/week remaining quota over App Server; real Windows CPU, primary GPU, physical RAM and dedicated VRAM over secured BLE. Missing values remain `--`; unsupported temperatures are unavailable. VS Code project names come only from a recognized foreground VS Code workspace title, retained while another app is foreground.
- Top blank-area tap (above the card, y < 135) or three minutes without input opens a three-hand analog clock with sleeping cat, animated ZZZ and one warm sentence. Card/side/bottom blanks, logo/status and cat taps do not manually enter idle. First touch returns to the previous card without triggering controls. Brightness stays unchanged; standby reduces UI work, not hardware frequency.
- Keep normal brightness, configured 360 MHz CPU and local dirty-region redraw for responsiveness, including the standby clock. No automatic dimming or CPU power-saving frequency scaling.

## Build and run

Install ESP-IDF 6.0.2 for ESP32-P4, then set `IDF_PATH` to that installation's
`esp-idf` directory. Set `IDF_TOOLS_PATH` only if the tools are not in the
standard per-user `.espressif` directory. Both paths can instead be supplied
as `-IdfPath` and `-IdfToolsPath` to the build script. From the repository root:

```powershell
.\scripts\build-firmware.ps1
```

[Windows companion setup](pc-agent/README.md) covers its Python environment,
BLE connection, login resident, and reproducible test commands. Install it
before starting the link.

For a local configuration change, run `.\scripts\build-firmware.ps1 -Action reconfigure`
before building. Flashing and monitoring require `-Port` with the port detected
on that machine; coordinate with the running BLE Agent before a device update.
A plain build does not open the serial port.
The bounded read-only UART observers also require an explicit `--port`; check
the current port before running either observer.

For persistent PC operation independent of the development tool, install the current-user Windows task once with `pc-agent/manage-ble-resident.ps1 -Action Install`, then `-Action Start`. It starts at login and recovers unexpectedly exited BLE workers without replaying music actions. See the PC companion README for status/stop/removal. The old USB task stays disabled.

The default is now the current B1 product (`build-ble-b1` / `sdkconfig.ble_b1`). `-BleB0` is an explicit secured-BLE diagnostic, not the removed USB/HTTP product. Do not flash historical binaries from the old ignored `build` directory.

The 10 MiB app partition preserves NVS, PHY and storage offsets. Never erase NVS/TF/C6 during routine updates. Wi-Fi credentials remain in ignored local sdkconfig; do not commit them.

## UI and verification

- [Interactive approved preview](docs/design/preview/index.html)
- [Documentation index](docs/README.md)
- [Current development status](docs/development/current-status.md)
- [New-session handoff](HANDOFF.md)
- [UI references](docs/design/ble-ui-references.md)
- [Current hardware record](docs/development/b1-music-bringup.md)
- [Performance and cleanup](docs/development/ui-performance-20261002.md)
- [Assets and font license](firmware/luna-panel/main/assets/README.md)

Local native LVGL tests do not replace inspection of the actual screen or physical touch testing.

## Retired implementation

The old USB/HTTP/audio implementation and its transport tests were removed after preserving their contents, including uncommitted changes, in a local backup outside the checkout. The old Windows scheduled task is disabled; the BLE resident is retained. Retired-route documents were also backed up locally before removal. These backups are maintainer recovery copies, not project build inputs or release artifacts. Current documentation is indexed above; retained BLE records are maintenance evidence, not configuration authority.
