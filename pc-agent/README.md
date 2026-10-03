# Luna Windows BLE companion

The active entry point is `luna_ble_link.py`. It uses paired, encrypted BLE named **Luna**; it never opens a COM port or listens on HTTP. Music metadata/control uses Windows GSMTC; volume/mute uses the system volume adapter. No album-cover stream is read.

```powershell
py -3 -m venv pc-agent/.venv-ble
.\pc-agent\.venv-ble\Scripts\python.exe -m pip install -r pc-agent/requirements-ble-music.txt
.\pc-agent\start-ble-link.ps1 -Music
```

For normal use, install the independent Windows-owned resident once:

```powershell
.\pc-agent\manage-ble-resident.ps1 -Action Install
.\pc-agent\manage-ble-resident.ps1 -Action Start
.\pc-agent\manage-ble-resident.ps1 -Action Status
# Intentional stop / removal:
# .\pc-agent\manage-ble-resident.ps1 -Action Stop
# .\pc-agent\manage-ble-resident.ps1 -Action Remove
```

`Luna BLE Agent` runs as the current interactive user, without elevation or a stored password. Windows starts it at login, not under the development tool's process lifetime. Its resident restarts an unexpectedly exited worker with bounded 2–30 second backoff and fresh handshake; it never replays music requests. Task Scheduler also restarts a failed resident after one minute. The existing link mutex still permits only one active session. No COM ports or HTTP listeners are opened. The task has no runtime/idle/battery timeout and is separate from the retired USB task. Log files: ignored `ble-resident.log`, `ble-link.log`, and native traceback-only `ble-crash.log`.

`-Action Status` shows both the scheduled task `State` and a `LinkState` derived
from the latest link log event. `Offline` means the latest event was a failed
connection attempt; `Connected` means a recent authenticated connection or
health exchange; `Starting` means a new worker has started. `Stale` means the
last such event is over 90 seconds old, and `Unknown` means no usable log is
available. Task `Running` alone does not mean Luna is connected. This is a
read-only log summary, not a live device probe.

Once installed, `start-ble-link.ps1` delegates background starts to this task. A foreground diagnostic is still explicit and must not run against the same device simultaneously.

For foreground logs, intentionally stop the resident first, run the diagnostic,
then start the resident again after closing the foreground process:

```powershell
.\pc-agent\manage-ble-resident.ps1 -Action Stop
.\pc-agent\start-ble-link.ps1 -Music -Foreground
# After Ctrl+C ends the foreground session:
.\pc-agent\manage-ble-resident.ps1 -Action Start
```

The launcher rejects `-Foreground` while the resident task is running, before
starting another BLE process. Stopping the resident for diagnostics also
temporarily stops PC state and music synchronization.

Do not run two BLE clients against Luna simultaneously. The single-instance guard prevents duplicate background residents. Uncertain music actions are never replayed after a disconnect.

Weather is obtained by the device over Wi-Fi, not by this PC process. `--music` now also starts independent cached dashboard collectors. The read-only `account/rateLimits/read` App Server call supplies Codex quota; windows are selected by actual durations (300/10080 minutes), never their array position. No inference requests, login changes, token copying, credit resets or thread-list scraping are used.

Windows metrics are sampled every 2 seconds with GetSystemTimes, GlobalMemoryStatusEx, DXGI and language-neutral PDH. GPU uses the busiest engine on the largest dedicated-memory hardware adapter; VRAM is adapter-wide dedicated usage, not per-process totals. CPU/GPU temperatures stay unavailable without a supported sensor provider. No driver installation or administrator access is required. Only a recognized default foreground `Code.exe` workspace title provides the last active project name; customized/unidentified titles remain unavailable. Full paths and source files are not sent. Open the intended VS Code project and bring it to the foreground once to populate this field.

Metric initialization retries with interruptible 2–30 second backoff. A failed GPU query is closed and reinitialized after a 30-second cooldown, including adapter re-enumeration; CPU/RAM/project sampling continues independently. Unsupported GPU providers remain unavailable, not zero. Failed PDH initialization releases its query, and repeated service starts do not create duplicate sampling threads.
If the whole PC metric sample fails three times in a row, the collector is closed and recreated with interruptible 2–30 second backoff. A single transient failure keeps the existing collector; the failed sample stays unavailable rather than appearing as zero. This recovery path has a simulated regression, but has not been exercised against a real Windows driver or sleep/wake failure, and the running resident may still have older code until intentionally restarted.

BLE negotiates the `dashboard` feature, sends cached read-only snapshots at most every 2 seconds, and prioritizes music actions. Existing music-only firmware is compatible and doesn't receive unsupported dashboard messages. Firmware validates the entire snapshot atomically; disconnected/expired readings are visibly marked. This collector never opens COM ports or an HTTP listener.

The old USB/HTTP entry points and login auto-start task have been retired. The task `Luna PC Agent` is disabled, not repurposed as BLE auto-start. Existing ignored local configuration/logs are preserved, not committed.

The quota adapter caches only quota windows (no thread/project listing or old
summary/reset strings). Its response queue is bounded and ignores unsolicited
notifications. Media metadata requests time out after 2 seconds; timed-out queued
actions are cancelled, but already submitted Windows operations cannot be undone.
Only the six explicit BLE commands are allowed; no toggle/volume-step fallback.
See [current documentation](../docs/README.md) for the active product and validation.

Tests (no real playback actions):

```powershell
py -3.10 -m venv pc-agent/.venv-test
.\pc-agent\.venv-test\Scripts\python.exe -m pip install -r pc-agent/requirements-test.txt
.\pc-agent\.venv-test\Scripts\python.exe scripts/run-pc-agent-tests.py --hosted
```

The hosted lane runs the Windows/Python tests without requiring ESP-IDF or a C
compiler. It includes mocked BLE and media tests; it does not connect to Luna
or command real playback. To run the complete local suite, install native GCC
and populate ESP-IDF managed components, then run the same script without
`--hosted`. Native tests compile firmware C code and must not be counted as
passed when these prerequisites are absent. The GitHub Actions workflow uses
Windows Server 2025 for its hosted lane and runs the dependency-free preview
model tests separately. The hosted lane mocks BLE and media calls; Windows
Server CI cannot validate Bleak's supported Windows 11 desktop runtime,
physical touch, BLE recovery, or a firmware build.
