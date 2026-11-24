# Real-LVGL desktop UI regression

This builds the actual B1 UI against the repository's managed LVGL 9.3 source.
Only time, BLE/music and weather services are simulated. Pointer input goes
through real LVGL input processing; widgets and event propagation are not mocked.
No PC playback commands, BLE connections, serial ports or host time writes occur.

Requires the existing managed components (created by an IDF configure), CMake,
Ninja and MinGW GCC/G++. Configure Debug so C assertions stay enabled:

```powershell
cmake -S scripts/native-ui -B .tools/native-ui-build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build .tools/native-ui-build --parallel 6
Push-Location .tools/native-ui-build
try { .\luna_ui_test.exe } finally { Pop-Location }
```

Run `luna_ui_test.exe --double-buffer` as well to exercise two DIRECT buffers,
LVGL's dirty-region synchronization and the clock-to-computer/standby-roundtrip
pixel regressions. Snapshots read the last submitted front buffer. This does not
simulate DSI scanout, DMA/cache coherence or physical panel retention.

Use explicit CMAKE_MAKE_PROGRAM/CMAKE_C_COMPILER/CMAKE_CXX_COMPILER paths if they
are not on PATH. `.tools` is ignored and contains generated PPM snapshots of the
five cards, vertical volume popup and idle dial. Convert formats with Pillow or
another image decoder for inspection; do not commit machine-generated captures.

This proves local input/state logic and rendering, not physical touch or LCD
appearance. Desktop RGB888 also differs from the hardware framebuffer's color
quantization. Fonts are Noto-derived; Windows web-font pixel equality is not claimed.
