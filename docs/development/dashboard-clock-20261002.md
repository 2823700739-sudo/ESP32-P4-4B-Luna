# Live dashboard and dual-entry analog clock — 2026-10-02

Implementation evidence for actual data sources and time policy. Current display,
standby and optimization settings supersede the dated entries below; see
[current-status.md](current-status.md).

Latest requirement supersedes the earlier no-standby choice: keep three-minute automatic idle AND add top blank-tap manual entry. DC operation remains always bright and full performance; no power management is added.

## Implementation

- Weather location: native full-Han 28 px instead of the scaled approximately 13 px label. Bounded/ellipsis layout prevents long names colliding with WEATHER.
- Codex: cached read-only official App Server `account/rateLimits/read`, prefer `rateLimitsByLimitId.codex`; map windows by duration 300/10080 minutes. Independently missing windows show `--`, genuine zero shows `0%`. No inference/auth/account mutations. Official schema: [App Server account rate limits](https://learn.chatgpt.com/docs/app-server#6-rate-limits-chatgpt).
- PC: user-mode CPU busy-time deltas, physical RAM, DXGI adapter identity/LUID/dedicated capacity and PDH WDDM busiest engine/dedicated usage. Select largest dedicated-memory hardware adapter (this PC: RX 6600), sum process utilization only within the same physical engine and select its maximum. Do not combine Intel and AMD or sum different engines. No temperature guessing. References: [PDH formatted arrays](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw), [language-neutral counters](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhaddenglishcounterw).
- VS Code: verify foreground executable is Code.exe, parse only recognized default workspace titles, retain last recognized workspace name while another application is foreground. Unidentified/customized/untitled windows don't invent a project. No extension installation or source-file reading. A workspace must be focused once; not an exact full-path VS Code extension bridge.
- Collectors run independently of BLE; reads return copied cache immediately. PC sampling / BLE telemetry at most every 2 s; quota refresh 15 s. Music FIFO/actions get priority; no command replay. Feature negotiation keeps older music-only firmware compatible.
- Device validates types, finite ranges, nullable sensors, string byte limits and memory relationships before atomic commit. PC age/receive age and quota observation timestamp drive offline/stale markings. Old values may be retained only with explicit last-data caption and dimmed indicators.
- Idle: 180 s without input or a genuine TOP blank tap (y < 135, above card); >12 px movement excludes manual entry. Card/side/bottom blanks, content, logo/status, buttons, pet, swipe, popup interactions excluded. Outside popup tap only closes it. First touch wakes to original card and waits for release. Local clock updates hands once per second, no every-second BLE message. Cards/pet/stars are hidden during clock display; no brightness/CPU/radio changes.

## Local validation (not hardware visual acceptance)

115 Python/C-core/PowerShell-scope tests passed; 27 preview model tests. Latest top-only browser regression: 57 checks passed (added card/bottom/side/logo/status exclusions).
Actual LVGL 9.3 native input/render test passed: dashboard indicators/null/stale state, 28 px location, dual standby entry, wake-only input, swipe/volume/pet regression. Native protected-font test: 12,000 parallel decodes, zero corruptions.
Native screenshots contain explicitly simulated samples; the web preview remains example-only and makes no Agent/network calls.

Read-only local live sampling succeeded: RX 6600 selected; real CPU/GPU/RAM/VRAM and real 300/10080-minute Codex windows returned. Temperature providers unsupported; no VS Code workspace foreground during initial sampling. Values are snapshots, not fixed constants.

Firmware built at 9,740,960 bytes (`0x94a2a0`), within existing 10 MiB app region. Initial dashboard SHA-256 `5b203ffd5d8ac2641e064154e745ce488b27475688a3897a191d030264ca5d17`; latest top-only entry build `715a5aa1897b05af440d492258813403da8901ee3d84e89e2d0cde1d94773870`. Only app at 0x20000 is flashed; NVS, bonds, partition table, TF and C6 firmware are preserved.

## Hardware/runtime verification

App-only COM27 transfer wrote 9,740,960 bytes, flash hash verified, then hard reset. Windows-owned BLE resident restored with existing bond; encrypted hello/time sync negotiated 253-byte ATT value. Device acknowledged a real dashboard at 20:46:20.792: CPU 1.6%, GPU 5.9%, five-hour remaining 36%, weekly remaining 59%. At 20:47:21.390 another accepted snapshot reported CPU 2.4%, same GPU/quota samples; health reached 197 music-business exchanges / 90 seconds. These percentages are historical samples, not hardcoded UI defaults.

Separate read-only COM27 observation was finite (35 s), with DTR/RTS disabled; no matched crash markers, but zero selected diagnostic lines means it is NOT proof of hardware stability or clock appearance. Observer closed/released COM27. BLE remains running for normal use, old USB task stays disabled, no HTTP fallback starts.

Physical touch/visual smoothness, real three-minute clock entry/wake and multi-hour radio stability remain user/device acceptance checks; native/virtual input tests are not presented as physical screen confirmation.

## Top-only refinement and maintenance

User photo explicitly restricts manual idle to blank space above the card. Input checks now require y < 135, exclude logo/status/pet/content, and reject manual entry from side/card/bottom blanks. Both native actual-LVGL input regression and preview browser exclusions passed. Automatic 180-second idle remains unchanged.

During flashing, found Task Scheduler can stop the venv launcher but leave real Python children. Updated the existing Stop/Remove commands to terminate only exact project guardian and background worker paths, guardian first. Native/foreground/unrelated Python and Codex applications are excluded; a mocked PowerShell test verifies targets, and a live Stop check confirmed zero matching guardian/worker processes before the top-only flash. This intentionally stops BLE only during maintenance; login auto-start/bonds are preserved.

Latest top-only app flashed successfully: 9,740,960 bytes at 0x20000, esptool verification passed and reset completed. Resident restarted via Windows task; COM27 opened with reset disabled and immediately closed to verify availability. No persistent serial observer remains.

Post-refinement BLE handshake authenticated at 20:57:15.004; accepted real dashboard at 20:57:15.424 (CPU 0.7%, GPU 16.1%, five-hour/week remaining 31%/58%). Health at 20:57:45.123: 69 music exchanges over 30 seconds. These are live protocol checks, not a claim that physical top-only touch has been user-tested yet.

## Readability and standby layout refinement

- TIME upper-right caption uses native 20 px Chinese instead of 13 px.
- Weather's three detail panels center both captions (20 px) and readings (28 px) within their existing 167 × 89 px bounds. The enlarged 28 px location remains unchanged.
- Removed only the Codex lower-right source footer; real quota, update time and current/recent VS Code project remain.
- Navigation grows from 56 × 48 to 64 × 58 px per item, with 24 px icons / 16 px labels. Arrow buttons grow to 48 px. The entire footer stays within y647–705, below the card and outside the roaming pet rail.
- Standby has a separate full 72 × 72 px sleeping cat, reusing existing frames 6/7 once per second. The normal roaming cat stays hidden. No new assets, full-screen animation, dimming or power management. Touching the sleeping cat follows the same wake-only input rule.
- Four clock digits move to approximately 144 px center radius, inside the 183 px major-tick tips; label bounds are regression-tested against the inner tick radius to prevent overlap.
- Automatic 180-second entry and top-only blank-tap entry are preserved, as are brightness, full-performance operation, BLE and Wi-Fi behavior.

Local verification: 64 headless browser checks, 27 model tests, 115 Python/C-core/scope tests passed. Actual LVGL render/input regression passed, including centered weather/font bounds, navigation sizing, sleeping sprite bounds, inner clock-number bounds and wake-only input. All newly enlarged Chinese captions have actual 20 px glyph coverage. Protected font decoder: 12,000 parallel decodes, zero corruptions. Native renders were visually reviewed for clock/weather/quota/standby; these use simulated data and are not hardware visual acceptance.

New app: 9,741,200 bytes (`0x94a390`), SHA-256 `6f25c414188bf94c5e56dc9921a38d6a9d9d0a74f1f335c10053bb6bc6d21dfc`. App-only flashing preserves NVS, bonds, partitions, TF and C6 firmware. Deployment/runtime results follow after verification.

Deployment: initial 460800-baud transfer lost connection and did not verify. Retried at 230400 baud, application-only write completed in 291.7 seconds; esptool flash hash verification passed, then hard reset. Windows BLE task was restored. COM27 was separately opened with DTR/RTS disabled and immediately closed; available and released. No persistent serial monitor.

Post-layout deployment BLE: authenticated hello/time sync at 21:24:25.432, ATT value 253 bytes. Device accepted live dashboard at 21:24:25.792 (CPU 2.1%, GPU 10.9%, five-hour/week remaining 21%/56%); 71 exchanges / 30 seconds healthy at 21:24:55.551. Crash log remains empty. These are protocol/runtime observations, not physical visual/smoothness acceptance.

## Menu bottom clipping and clock-number offset

User reported clipped lower navigation labels and requested a slightly more outward clock numeral placement, then clarified moving the entire lower menu upward. CJK16's actual line box is 24 px: the previous y37 placement exceeded the 58 px button by 3 px. Labels now start at y32 (2 px bottom margin), icons at y4. Whole footer moves from y647 to y639 (8 px upward), with arrows/nav moving together; item/font/icon sizes remain unchanged. Standby numeral centers move 8 px outward to approximately 152 px radius, with 40 px centered label boxes still strictly inside major tick tips. Sleeping cat and dual idle entry are preserved.

66 browser checks and actual LVGL regression passed, including the entire 24 px menu-label box inside its button and screen, y639 footer position, numeral label bounds inside radius181, wake-only touch, swipe and pet controls. Native clock/idle render images visually reviewed. 27 model / 115 Python tests and protected 12,000-glyph decode test passed earlier in this same refinement; no subsequent data/decoder/model changes. Physical visual acceptance remains separate.

Final refinement build: 9,741,200 bytes, SHA-256 `b64f8f2cc403e9606076abae17cea2141ba23346f87d3345e0fa78fd5d5e9b68`. App-only deployment uses 230400 baud and preserves existing NVS/bond/partition/TF/C6 data.

Refinement app-only flash completed in 291.9 seconds; esptool hash verified and hard reset completed. Restored Windows BLE task, verified COM27 can open with DTR/RTS disabled and closed it immediately. No serial monitor remains.

BLE reauthenticated and synced time at 21:37:51.377 (253-byte ATT value), device accepted real dashboard at 21:37:51.798 (CPU 0.8%, GPU 10.9%, remaining five-hour/week 8%/54%). Crash log empty. Physical layout acceptance remains for device observation.
