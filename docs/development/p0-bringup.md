# P0 bring-up: revision 1.3 board

## Fixed hardware assumptions

- Board: ESP32-P4-86-Panel-ETH-2RO, SKU 31570, with 86 Panel Bottom Board.
- ESP32-P4 silicon revision: 1.3.
- Power: USB-C.
- Network for the first milestone: 2.4 GHz Wi-Fi through the onboard ESP32-C6.
- Ethernet: retained in the architecture, deferred until a suitable cable is available.
- Storage: MicroSD/TF card is present.
- Audio: bundled 8 ohm / 2 W speaker is present; PC speakers play music.

## Toolchain baseline

Use the existing ESP-IDF v6.0.2 checkout at
`D:\.espressif\v6.0.2\esp-idf`. Waveshare's current examples support v6.0.2;
the Brookesia example is the exception and is not used by Luna P0. Do not build
this board with the reference repository's default revision 3.x settings. The
Luna project explicitly selects the pre-v3 family and 200 MHz PSRAM for
revision 1.3.

After opening an ESP-IDF PowerShell, verify:

```powershell
idf.py --version
```

The result should report ESP-IDF v6.0.2. The project path contains no spaces,
which satisfies the ESP-IDF Windows path requirement.

For a repeatable build from an ordinary PowerShell window, run from the
repository root:

```powershell
.\scripts\build-firmware.ps1
```

脚本会遮盖非交互式构建输出中含本地 Wi-Fi 配置和 Agent 配对配置的行，
避免复制构建日志时带出凭据。`menuconfig` 是交互式配置界面，不经过日志过滤。

## Configure Wi-Fi without committing credentials

From `firmware/luna-panel`:

```powershell
idf.py set-target esp32p4
idf.py menuconfig
```

Open `Luna P0 Configuration` and enter the 2.4 GHz Wi-Fi SSID and password.
These values are written to the generated `sdkconfig`, which is ignored by Git.

## Build, flash, and monitor

Connect the board's **USB TO UART** Type-C port. Its current Windows port is
`COM27`. From the repository root, run:

```powershell
.\scripts\build-firmware.ps1
.\scripts\build-firmware.ps1 -Action flash-monitor
```

If Windows assigns another port later, override it with `-Port COMx`. Exit the
monitor with `Ctrl+]`.

Do not run `erase-flash` during ordinary iteration. It would also remove saved
configuration and is unnecessary for this test.

### Recover from `invalid header`

If the ROM repeatedly reports an error such as
`invalid header: 0x4f4e5f48`, the application has not started. The P4 expects
an ESP image header at the bootloader offset `0x2000`, but the flash contents at
that location are not a valid bootloader. This is usually caused by flashing
only the application binary or using offsets for a different ESP chip.

Create one image containing the bootloader, partition table, and application,
then write it at offset `0x0`:

```powershell
.\scripts\build-firmware.ps1 -Action merge
.\scripts\build-firmware.ps1 -Action flash-full -Port COM27
```

`flash-full` builds and regenerates `build\luna_panel_full.bin` before writing
it, so the command cannot use a stale application with a new partition table.
It writes the following ESP32-P4 layout:

| Image | Offset |
| --- | ---: |
| Bootloader | `0x2000` |
| Partition table | `0x10000` |
| Luna application | `0x20000` |

Do not select `luna_panel.bin` at address `0x0` in a graphical flash tool. If a
graphical tool is required, select `luna_panel_full.bin` at `0x0`, with ESP32-P4,
DIO, 80 MHz, and 32 MB flash settings. A full-chip erase is only a fallback if
the verified merged image still produces the same ROM error.

## Expected hardware-check screen

1. `Luna hardware check` appears on a dark background.
2. `Wi-Fi`, `TF card`, and `Audio` change from `starting` to their result.
3. Wi-Fi success displays the assigned IPv4 address in the detail line.
4. TF-card success displays its name, capacity, and `read/write OK`.
5. Audio success enables `Test speaker`; tapping it plays a short 880 Hz tone.
6. Speaking or tapping near the microphones moves the green `Mic` level bar.
7. Tapping `Tap to test touch` increases its counter.

The microphone uses the Waveshare reference input gain of 24 dB. Its meter is
mapped logarithmically from -52 dBFS to -8 dBFS, with fast attack and slower
release so ordinary speech at desktop distance remains visible. If a quiet room
keeps the meter high, adjust `Microphone input gain (dB)` under
`Luna P0 Configuration`; try 18 dB before changing the meter thresholds.

The TF-card test does not format the card. It creates, verifies, and deletes
`/sdcard/.luna_probe.tmp`. A mount failure is reported without changing the
card filesystem.

If the screen works but Wi-Fi fails, capture the full serial log from reset to
the first failure. The P4 host dependencies are pinned, but the factory C6
firmware still has to be compatible with that host protocol.

## Validation record

Record the result in this table after each flash:

| Check | Expected | Actual |
| --- | --- | --- |
| Flash on COM27 | Completes without error | Passed |
| Boot | No chip-revision or PSRAM error | Passed |
| Display | Stable 720 x 720 UI | Passed |
| Touch | Counter increments at tapped position | Passed |
| Wi-Fi | Connects and obtains IPv4 address | Passed |
| TF card | Shows capacity and `read/write OK` | Deferred: card not inserted |
| Speaker | Test button produces a short tone | Passed |
| Microphones | Level bar responds to nearby speech | Passed at 24 dB input gain |
| Restart | Repeats successfully after reset | Passed |

P0 hardware bring-up passed on 2026-09-25, except for the intentionally deferred
TF-card check. The next gate adds the Windows companion protocol and uses the
same audio path for the first wake-word experiment.
