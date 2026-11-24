# Luna deployment guide

[中文](DEPLOYMENT.md) · **English** · [Product introduction](../README.en.md)

This guide targets **2026.10.08-preview**. The repository includes source and public binaries. The public image contains no Wi-Fi credentials or device NVS data. Clock, music, quota and PC metrics use BLE. Network weather requires a local build, and an initial weather-location setup entry point is not yet available.

The merged-image distribution is `2026.11.24-preview`; the embedded binary version remains `2026.10.08-preview`. Use the [complete source, binary and license ZIP](https://github.com/2823700739-sudo/ESP32-P4-4B-Luna/archive/refs/tags/v2026.11.24-preview.zip). Separate binary distributions must include [LICENSE](../LICENSE), [NOTICE](../NOTICE), [third-party notices](../THIRD_PARTY_NOTICES.md) and the complete [LICENSES/](../LICENSES/) directory.

## 1. Requirements and compatibility

- Waveshare ESP32-P4-WIFI6-Touch-LCD-4B, 720×720, P4 rev1.x, 32 MB Flash. Do not flash this image onto other boards, rev3.x or other displays.
- The onboard C6 must support the existing ESP-Hosted / HCI path. No C6 binary or C6 update is included.
- Windows 11 x64 (build ≥ 22000), Bluetooth, 64-bit x64 Python 3.11 and a data cable connected to the board's download/serial interface. Use the actual COM port on your PC.
- First installation writes the P4 bootloader and partition table. Check layout and back up important data on devices running other firmware. Normal Luna upgrades write only the app. Do not erase the whole chip or bypass compatibility checks with `--force`.

Get the Python 3.11 Windows installer (64-bit) from the [official downloads](https://www.python.org/downloads/windows/) and enable Python Launcher. New installations use `py -3.11`; existing Python 3.10 residents need not be recreated for this packaging update.

Download and extract this version's full repository ZIP, or:

```powershell
git clone --depth 1 --branch v2026.11.24-preview https://github.com/2823700739-sudo/ESP32-P4-4B-Luna.git
cd ESP32-P4-4B-Luna
```

Run the commands below from the repository root. When downloading a binary individually, use GitHub's **Download raw file**, rather than saving the HTML page as a bin.

## 2. Files and checksums

Files are in the [firmware directory](../firmware/releases/2026.10.08-preview/README.md):

| File | Purpose | Offset |
| --- | --- | --- |
| `luna-first-install-0x0.bin` | First installation only; overwrites NVS/bonds, not a data-preserving upgrade | `0x0` |
| `luna-panel.bin` | Five-card B1 app; use for an existing Luna upgrade | `0x20000` |
| `bootloader.bin` | P4 bootloader for first installation | `0x2000` |
| `partition-table.bin` | 10 MiB app partition layout for first installation | `0x10000` |
| `manifest.json` / `SHA256SUMS.txt` | Version, hardware, offsets, sizes and SHA-256 | Not flashed |

```powershell
Get-Content firmware/releases/2026.10.08-preview/SHA256SUMS.txt
Get-FileHash firmware/releases/2026.10.08-preview/*.bin -Algorithm SHA256
```

The hashes of all downloaded binaries must match. Do not download or flash the maintainer's NVS, bonds, TF contents, private configuration or full-chip dump.

## 3. Install the flashing tool

A full ESP-IDF installation is unnecessary for the public binaries. Use a separate environment:

```powershell
py -3.11 -m venv .venv-flash
.\.venv-flash\Scripts\python.exe -m pip install esptool==5.4.0
```

If the BLE resident is already installed, stop it before flashing:

```powershell
.\pc-agent\manage-ble-resident.ps1 -Action Stop
```

Replace `COMx` below with the actual port. It is a placeholder, not a usable port name. If download mode does not activate, follow the board vendor's BOOT / RESET procedure. Do not use the C6 download interface.

If PowerShell blocks a `.ps1` file, invoke that command using `powershell -NoProfile -ExecutionPolicy Bypass -File .\pc-agent\manage-ble-resident.ps1 -Action Stop` (substitute the intended Action). This is process-scoped, without a global policy change. Contact your administrator if organizational policy prohibits it.

## 4. Upgrade an existing Luna: app only

Use this only for the confirmed B1 layout: app at `0x20000`, capacity 10 MiB, storage at `0xA20000`. It does not apply to the old 6 MiB app layout or another product's partitions.

```powershell
.\.venv-flash\Scripts\python.exe -m esptool --chip esp32p4 --port COMx write-flash `
  --flash-mode keep --flash-freq keep --flash-size keep `
  0x20000 firmware/releases/2026.10.08-preview/luna-panel.bin
```

This writes only the app, retaining the partition table, NVS, TF and C6 firmware. **The public app has empty Wi-Fi credentials.** To retain network weather, flash a locally configured private app instead. The public app cannot inherit a password compiled into your previous app.

## 5. First installation: one binary at 0x0

Use this only for a **first-install device whose existing data may be cleared**. Verify the board, P4 **rev1.x**, 32 MB Flash and B1 layout. The onboard C6 must already run compatible ESP-Hosted/HCI firmware. The merged file includes only the P4 bootloader, partition table and app; **it cannot provision a blank C6**. Stop if C6 is incompatible; do not flash arbitrary C6 images. Fresh-device hardware acceptance is still pending.

**RAW gaps contain `0xFF`. Writing the merged file at `0x0` clears NVS at `0x11000–0x16FFF`, including BLE bonds, weather location and cache; it also overwrites phy_init. Existing devices that must retain data must use section 4's app-only upgrade.** Gap sectors are erased even without `erase-flash`; see [esptool RAW format](https://docs.espressif.com/projects/esptool/en/latest/esp32p4/esptool/basic-commands.html#raw-output-format).

Check the actual chip revision and Flash size first; **proceed only with P4 rev1.x and 32 MB**:

```powershell
.\.venv-flash\Scripts\python.exe -m esptool --chip esp32p4 --port COMx flash-id
```

After checking SHA-256, select only this file at `0x0` (`0x00` is equivalent):

```powershell
.\.venv-flash\Scripts\python.exe -m esptool --chip esp32p4 --port COMx write-flash `
  --flash-mode keep --flash-freq keep --flash-size keep `
  0x0 firmware/releases/2026.10.08-preview/luna-first-install-0x0.bin
```

DIO / 80 MHz / 32 MB settings are already embedded. No local merge or ESP-IDF installation is needed. An existing P4-compatible graphical download tool can likewise select just this file at `0x0`; disable whole-chip erase and do not use the app offset `0x20000`. The raw file begins with padding, so do not rely on automatic first-header checks to validate every embedded image's silicon revision. Verify hardware before writing.

The image is `9569312` bytes (`0x920420`); sector erasure ends at `0x921000` exclusive. It is not padded to 32 MB. This write range does not touch storage at `0xA20000`, TF or the separate C6 Flash; NVS is still overwritten.

### Optional: separate-file installation

Use the following three files **instead of the merged file**, not in addition. This avoids padding the NVS gap but cannot guarantee compatibility with another product's NVS; it is not a lossless migration procedure for arbitrary old firmware.

```powershell
.\.venv-flash\Scripts\python.exe -m esptool --chip esp32p4 --port COMx write-flash `
  --flash-mode keep --flash-freq keep --flash-size keep `
  0x2000 firmware/releases/2026.10.08-preview/bootloader.bin `
  0x10000 firmware/releases/2026.10.08-preview/partition-table.bin `
  0x20000 firmware/releases/2026.10.08-preview/luna-panel.bin
```

Release BOOT, restart P4 and retain DC power. The five-card UI, touch navigation and Bluetooth initialization should appear. The clock needs Agent time sync; flashing does not configure weather or quota. Stop on chip incompatibility, NVS failure or C6 controller unavailability; do not bypass checks with force or whole-chip erase.

## 6. Pair and install the Windows Agent

Create an independent Agent environment:

```powershell
py -3.11 -m venv pc-agent/.venv-ble
.\pc-agent\.venv-ble\Scripts\python.exe -m pip install -r pc-agent/requirements-ble-music.txt
.\pc-agent\.venv-ble\Scripts\python.exe -m pip check
.\pc-agent\.venv-ble\Scripts\python.exe pc-agent/check-deployment.py
```

The check must report `Agent prerequisites OK`; correct versions/dependencies before starting the resident. It does not scan Bluetooth, pair or control media. Enable Bluetooth in Windows; a disabled adapter or denied scanning access still prevents connectivity.

1. Add **Luna** in Windows Bluetooth settings.
2. Compare the numbers on Windows and Luna, and approve both only if they match.
3. Install and start the current-user login resident:

```powershell
.\pc-agent\manage-ble-resident.ps1 -Action Install
.\pc-agent\manage-ble-resident.ps1 -Action Start
.\pc-agent\manage-ble-resident.ps1 -Action Status
```

The clock appears once valid time is received, and metrics update from background sampling. Music needs a GSMTC-capable player. Quota requires an existing local Codex CLI and login; missing quota does not prevent the other functions. Bring the intended VS Code workspace to the foreground once.

Minimum setup checks: touch navigation works; after pairing, `LinkState=Connected`; the clock synchronizes and CPU/RAM have readings. Music needs GSMTC, quota needs an existing usable Codex environment, and initial weather-location setup is missing. All five cards are not guaranteed to have online data on a new device.

Task `State=Running` does not establish a device connection. Check `LinkState=Connected` and recent logs:

```powershell
Get-Content pc-agent/ble-link.log -Tail 12
```

The resident runs as the current interactive user without elevation or stored passwords. It starts at login and retries after worker failure. Do not run a second BLE client. To stop or remove:

```powershell
.\pc-agent\manage-ble-resident.ps1 -Action Stop
.\pc-agent\manage-ble-resident.ps1 -Action Remove
```

## 7. Build from source

Install ESP-IDF **6.0.2** and the ESP32-P4 toolchain. Set `IDF_PATH`; set `IDF_TOOLS_PATH` if tools are outside the default per-user .espressif directory. The wrapper also accepts `-IdfPath` / `-IdfToolsPath`.

### Credential-free public build

```powershell
.\scripts\build-firmware.ps1 -PublicRelease -Action reconfigure
.\scripts\build-firmware.ps1 -PublicRelease -Action build
```

This uses isolated `sdkconfig.public_b1` and `build-public-b1`, rejects nonempty Wi-Fi credentials, and disallows flash/monitor actions. It retains 360 MHz and the current double-buffer display. The app is `firmware/luna-panel/build-public-b1/luna_panel.bin`; bootloader and partition-table binaries are in their corresponding subdirectories. Audit files, provenance and credentials before publishing.

### Reproduce the public merged file (maintainers, optional)

With the three published credential-free inputs, use the flashing environment to reproduce the package. Never replace those inputs with private network-enabled builds:

```powershell
.\.venv-flash\Scripts\python.exe scripts/merge-public-firmware.py
.\.venv-flash\Scripts\python.exe scripts/merge-public-firmware.py --check
```

The script verifies input offsets, sizes, SHA-256 and B1 partitions, then checks original segment bytes and padding. It accesses no device and does not pad to full Flash size.

### Private network-enabled build

```powershell
.\scripts\build-firmware.ps1 -BleB1 -Action reconfigure
```

In an initialized ESP-IDF terminal, enter `firmware/luna-panel`:

```powershell
idf.py -B build-ble-b1 -D SDKCONFIG=sdkconfig.ble_b1 menuconfig
```

Enter your 2.4 GHz SSID and password under **Luna Current Product**. Return to the repository root:

```powershell
.\scripts\build-firmware.ps1 -BleB1 -Action reconfigure
.\scripts\build-firmware.ps1 -BleB1 -Action build
```

Your app is `firmware/luna-panel/build-ble-b1/luna_panel.bin`. It contains the configured Wi-Fi credentials: use it only on your own device and do not upload it. For a matching existing layout, use section 4 with this app path instead of the public app.

An existing NVS weather location is retained. **A new device currently has no location setup entry point**; setting Wi-Fi alone does not supply a location. The timezone is fixed to Beijing time.

## 8. Troubleshooting

| Symptom | Action |
| --- | --- |
| No COM port | Check the data cable, download interface and Windows Device Manager; use the actual detected port |
| Chip or layout mismatch | Stop; verify P4 rev1.x and the B1 10 MiB layout, without force or whole-chip erase |
| Running task, offline cards | Check LinkState and recent ble-link.log, device power/advertising, Bluetooth and pairing; avoid concurrent clients |
| Pairing mismatch or old bond | Reject mismatched numbers; use only the explicit on-screen bond-recovery action when required, never erase all NVS |
| Weather location unset | Initial location setup is not implemented; an Agent change or new service does not supply this missing entry point |
| Pairing required after merged install | RAW gaps cleared NVS. Remove that Luna's stale Windows bond and confirm new matching numbers. Use app-only for routine upgrades |
| Python / dependency check fails | Use Windows x64, 64-bit Python 3.11 and requirements-ble-music.txt in the same .venv-ble; pass pip check and check-deployment.py first |
| Public bin has no network | Credentials are intentionally empty; make a local private build and do not upload that image |
| Unknown music/quota/workspace | Check GSMTC support, local Codex availability and the default VS Code title; unknown is not zero |

See the [introduction](../README.en.md#current-limitations-and-licensing) for licensing and the [development guide](DEVELOPMENT.md) for architecture.
