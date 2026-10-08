# 第三方许可与致谢 / Third-party licenses and acknowledgements

[中文项目介绍](README.md) · [English introduction](README.en.md) · [Project LICENSE](LICENSE) · [NOTICE](NOTICE)

## 许可范围 / License scope

Luna 自有代码与文档使用 **Apache-2.0**。有明确其他许可头的文件继续沿用原许可：`luna_ble_frame.c`、`luna_ble_frame.h`、诊断用 `luna_ble_b0.c` 为 [CC0-1.0](LICENSES/CC0-1.0.txt)。Noto 派生字库及第三方组件不会因根目录许可证而改为 Apache。像素猫的生成与加工来源见[资源说明](docs/design/preview/assets/README.md)；Windows 系统字体文件没有打包。

Luna-owned code and documentation use **Apache-2.0**. Explicit per-file licenses take precedence: the three BLE files above retain CC0-1.0. Derived Noto fonts and third-party components retain their original terms. Pixel-cat provenance is documented in the linked asset notes; Windows font files are not bundled.

`LICENSES/` 保留上游许可原文，包含完整版权、条件及免责文字；[许可清单](LICENSES/manifest.json)记录实际版本、来源路径、链接及每份文件 SHA-256。固件版本来自[组件锁文件](firmware/luna-panel/dependencies.lock)，Windows 依赖来自 [BLE](pc-agent/requirements-ble.txt) / [音乐](pc-agent/requirements-ble-music.txt)清单。仅摘录源码头的两份文件在清单中明确标注。组件源码由 ESP-IDF Component Manager 下载，不把本机依赖目录或工具链上传。

`LICENSES/` contains upstream license and notice texts, including copyrights, conditions and disclaimers. Its manifest records versions, upstream paths/URLs and SHA-256 values. Two extracted source-header notices are identified explicitly. Firmware dependencies are downloaded by ESP-IDF Component Manager; installed component directories and toolchains are excluded from the repository.

## 固件主要组件 / Main firmware components

| Component / 组件 | Version / 版本 | Role / 用途 | License text / 原文 |
| --- | --- | --- | --- |
| ESP-IDF | 6.0.2 | ESP32-P4 SDK | [Apache-2.0](LICENSES/esp-idf/LICENSE); bundled libraries below retain their licenses |
| Waveshare ESP32-P4-WIFI6-Touch-LCD-4B BSP | 3.0.1 | Board support / 板级支持 | [Apache-2.0](LICENSES/components/waveshare__esp32_p4_wifi6_touch_lcd_4b/LICENSE) |
| Waveshare ST7703 | 2.0.0 | LCD driver / 屏幕驱动 | [MIT](LICENSES/components/waveshare__esp_lcd_st7703/license.txt) |
| LVGL | 9.3.0 | Touch UI / 触控界面 | [MIT](LICENSES/components/lvgl__lvgl/LICENCE.txt) |
| esp_lvgl_adapter | 0.6.3 | Display integration / 显示适配 | [Apache-2.0](LICENSES/components/espressif__esp_lvgl_adapter/LICENSE) |
| esp_lcd_touch / esp_lcd_touch_gt911 | 1.2.1 / 1.2.1 | Touch input / 触摸输入 | [Apache-2.0](LICENSES/components/espressif__esp_lcd_touch/license.txt), [GT911 text](LICENSES/components/espressif__esp_lcd_touch_gt911/license.txt) |
| ESP-Hosted / esp_wifi_remote | 2.12.11 / 1.6.3 | Existing C6 wireless transport / 现有 C6 无线链路 | [Apache-2.0](LICENSES/components/espressif__esp_hosted/LICENSE), [remote text](LICENSES/components/espressif__esp_wifi_remote/LICENSE) |
| cJSON | 1.7.19~2 | JSON parsing / JSON 解析 | [MIT](LICENSES/components/espressif__cjson/LICENSE) |
| libpng / zlib | 1.6.58~2 / 1.3.2~1 | Image/compression dependencies / 图像及压缩依赖 | [libpng](LICENSES/components/espressif__libpng/LICENSE), [zlib](LICENSES/components/espressif__zlib/LICENSE) |
| Noto Sans CJK → Luna bitmap fonts | 16/28 px Chinese and static/digit fonts | Bundled device glyphs / 设备字库 | [SIL OFL-1.1](LICENSES/fonts/OFL-NotoSansCJK.txt), [conversion details](firmware/luna-panel/main/assets/README.md) |

锁文件还包含 button、cmake_utilities、eppp_link、esp_codec_dev、esp_lv_decoder、esp_lv_fs、esp_mmap_assets、esp_new_jpeg、esp_serial_slave_link、FreeType、knob、usb、wifi_remote_over_eppp。各版本及许可见[完整清单](LICENSES/manifest.json)。这些是已解析的直接/传递依赖，并不代表每个组件都被链接或启用了产品功能；没有增加麦克风、语音、OTG 或其他界面。

The lockfile also resolves the components listed above. Their versions and texts are in the manifest. A resolved dependency does not imply that every optional library is linked or that its corresponding product feature is enabled.

特别条款：`esp_new_jpeg` 的 [ESPRESSIF MIT License](LICENSES/components/espressif__esp_new_jpeg/LICENSE)有“用于 Espressif 产品”的限制，不能概括为无条件标准 MIT。`wifi_remote_over_eppp` 的运行源码标注 Apache-2.0，[摘录头](LICENSES/components/espressif__wifi_remote_over_eppp/runtime-source-header.txt)与 [Apache 原文](LICENSES/esp-idf/LICENSE)一并保留；它不是当前采用的 Hosted 链路。FreeType 当前公开 app 未链接，若启用则采用 [FreeType License](LICENSES/components/espressif__freetype/freetype/docs/FTL.TXT)选项，保留[上游双许可说明](LICENSES/components/espressif__freetype/freetype/LICENSE.TXT)及 GPL 替代选项原文。Portions of this software are copyright 1996-2026 The FreeType Project (https://freetype.org). All rights reserved.

Special terms: `esp_new_jpeg` has an Espressif-product use restriction; its text is not unrestricted standard MIT. `wifi_remote_over_eppp` runtime sources carry Apache-2.0 notices and are not the selected Hosted transport. FreeType is not linked in the current public app; the FTL option applies if enabled in this project, with upstream alternative-license texts retained.

## SDK 内含库与工具链运行库 / SDK libraries and toolchain runtime

ESP-IDF 的总许可不覆盖所有内含库。随本版保留：[FreeRTOS MIT](LICENSES/esp-idf/components/freertos/FreeRTOS-Kernel/LICENSE.md)、[NimBLE Apache-2.0](LICENSES/esp-idf/components/bt/host/nimble/nimble/LICENSE)及[原 NOTICE](LICENSES/esp-idf/components/bt/host/nimble/nimble/NOTICE)、[Mbed TLS 双许可](LICENSES/esp-idf/components/mbedtls/mbedtls/LICENSE)及 [TF-PSA-Crypto](LICENSES/esp-idf/components/mbedtls/mbedtls/tf-psa-crypto/LICENSE)（本项目采用 Apache-2.0 选项）、[lwIP BSD](LICENSES/esp-idf/components/lwip/lwip/COPYING)、[HTTP parser MIT](LICENSES/esp-idf/components/http_parser/LICENSE.txt)、[linenoise BSD](LICENSES/esp-idf/components/console/linenoise/LICENSE)、[argtable3 BSD](LICENSES/esp-idf/components/console/argtable3/LICENSE)、[SPIFFS MIT](LICENSES/esp-idf/components/spiffs/spiffs/LICENSE)、[FatFs 原始许可头](LICENSES/esp-idf/fatfs-source-header.txt)。

ESP-IDF's umbrella license does not replace the licenses of bundled libraries. The texts linked above retain their original terms; this project uses the Apache option for Mbed TLS and TF-PSA-Crypto.

HTTPS 根证书包包含 Mozilla CA 数据，采用 [MPL-2.0](LICENSES/certificate-bundle/MPL-2.0.txt)。本版同时提供用于生成证书包的完整公开源数据 [cacrt_all.pem](LICENSES/certificate-bundle/cacrt_all.pem)（Mozilla 2025-12-02 快照）与 ESP-IDF 的公开补充 [cacrt_local.pem](LICENSES/certificate-bundle/cacrt_local.pem)；这些是公共 CA 证书，不含私钥或设备身份。来源与许可说明见 [curl CA extract](https://curl.se/docs/caextract.html#license)。

The HTTPS root bundle includes Mozilla CA data under MPL-2.0. Its complete public PEM inputs are included above, retaining source headers. They are public trust anchors, with no private keys or device identities. They are certificate data, not a relicensing of Luna's application code.

公开 bin 使用 Espressif GCC **esp-15.2.0_20251204**。`libgcc` / `libstdc++` 的 [GPLv3](LICENSES/toolchain-runtime/gcc/COPYING3)与 [GCC Runtime Library Exception 3.1](LICENSES/toolchain-runtime/gcc/COPYING.RUNTIME)同时保留；例外允许符合其条件的独立程序采用自身许可。C 运行库保留 [picolibc](LICENSES/toolchain-runtime/picolibc/COPYING.picolibc)与 [newlib](LICENSES/toolchain-runtime/picolibc/COPYING.NEWLIB)多项原始通知。这些许可不表示整个 Luna 改为 GPL。

Public binaries use that Espressif GCC toolchain. GPLv3 and the GCC Runtime Library Exception are both included for relevant GCC runtime code; the exception permits independent modules to retain their own license when its conditions are met. Picolibc/newlib retain their multipart notices. Luna's own code remains Apache-2.0.

## Windows Agent 与开发工具 / Windows Agent and development tools

| Component / 组件 | Version / 版本 | Scope / 范围 | License text / 原文 |
| --- | --- | --- | --- |
| Bleak | 3.0.2 | BLE runtime / BLE 运行依赖 | [MIT](LICENSES/python/bleak/LICENSE) |
| PyWinRT: winrt-runtime and winrt-* | 3.2.1 | Windows BLE/GSMTC bindings / Windows API 绑定 | [MIT](LICENSES/python/pywinrt/LICENSE) |
| async-timeout | 5.0.1 | Runtime timeout / 运行超时 | [Apache-2.0](LICENSES/python/async-timeout/LICENSE) |
| typing_extensions | 4.16.0 | Runtime typing / 类型兼容 | [PSF license and history](LICENSES/python/typing-extensions/LICENSE) |
| pyserial | 3.5 | Development diagnostics only / 开发诊断 | [BSD-3-Clause](LICENSES/python/pyserial/LICENSE.txt) |
| lv_font_conv | 1.5.3 | Offline font conversion only / 离线字库生成 | [MIT](LICENSES/tools/lv_font_conv/LICENSE) |
| sharp | 0.35.5 | Offline asset generation only / 离线资源生成 | [Apache-2.0](LICENSES/tools/sharp/LICENSE) |

Python/Node 解释器、工具链、生成器依赖树和 esptool 刷写工具均不在此仓库中分发；使用时按官方安装包与各自许可获取。普通固件构建使用已提交的字库 C 文件，不需要重新运行资源生成器。

Python/Node interpreters, toolchains, generator dependency trees and esptool are not redistributed here. Obtain them through their official packages with their respective licenses. Normal firmware builds use the checked-in glyph data without regenerating assets.

Windows、Codex 和 Open-Meteo 是集成的系统/服务，不是此仓库重新授权的开源代码；使用者须遵守各自条款。天气数据来源见 [Open-Meteo attribution](https://open-meteo.com/en/docs#attribution)。

Windows, Codex and Open-Meteo are integrated systems/services, not open-source code relicensed by this repository. Their own terms apply, including Open-Meteo's data attribution requirements.

## 分发 / Redistribution

推荐下载本版完整源码 ZIP，它包含源代码、bin 和全部许可文件。仅下载或转发 bin 时，也应同时附带根目录 `LICENSE`、`NOTICE`、本文件和完整 `LICENSES/`，保留字体 OFL、原始版权与条件。修改或重新构建后需重新核对实际版本、启用组件及其通知；不能直接照搬本版清单。

The complete source ZIP includes source, binaries and license materials. When distributing binaries separately, accompany them with the root `LICENSE`, `NOTICE`, this file and the complete `LICENSES/` directory. Retain font OFL and original notices. Re-audit actual versions, enabled components and notices after modifications or rebuilds.
