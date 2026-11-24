# Luna Windows BLE Agent

[Deployment guide](../docs/DEPLOYMENT.en.md) · [部署教程](../docs/DEPLOYMENT.md)

`luna_ble_link.py` connects to paired, authenticated Luna over BLE. Windows GSMTC supplies music metadata and explicit playback controls; Core Audio handles system volume/mute. Background collectors supply CPU/GPU/RAM/VRAM, local Codex quota and the most recent foreground VS Code workspace title. The process opens no serial port or HTTP listener.

## Install and run

From the repository root:

```powershell
py -3.10 -m venv pc-agent/.venv-ble
.\pc-agent\.venv-ble\Scripts\python.exe -m pip install -r pc-agent/requirements-ble-music.txt
```

Pair Luna in Windows Bluetooth settings and confirm matching numbers on both devices, then:

```powershell
.\pc-agent\manage-ble-resident.ps1 -Action Install
.\pc-agent\manage-ble-resident.ps1 -Action Start
.\pc-agent\manage-ble-resident.ps1 -Action Status
```

The resident runs as the current interactive user at login, without elevation or a saved password. A single-instance guard permits one BLE client. An exited worker restarts with bounded backoff and a new handshake, without replaying uncertain music commands.

Task Running is separate from a connected device. Inspect LinkState and recent `pc-agent/ble-link.log` entries. Stop the resident before foreground diagnostics; close the foreground process and restore it afterward:

```powershell
.\pc-agent\manage-ble-resident.ps1 -Action Stop
.\pc-agent\start-ble-link.ps1 -Music -Foreground
# After Ctrl+C:
.\pc-agent\manage-ble-resident.ps1 -Action Start
```

Weather belongs to the device, not this Agent. Optional quota uses read-only `account/rateLimits/read` on an existing local Codex App Server; it does not change login or request inference. Missing sources remain unknown. Workspace recognition uses the default VS Code title and does not transmit full paths or source files.

Logs, virtual environments, credentials and private configuration are excluded from Git. See the deployment guide for hardware compatibility, setup, stopping/removal and troubleshooting.
