# Luna approved-preview resources

These generated resources port the local, approved `docs/design/preview` artwork.
Do not substitute a newly designed UI. The screen is 720×720; cards are 590×450
at (65,135), with the preview's decorative neighboring edges. Only the volume
popup intentionally differs from the old preview: the user's vertical-slider
request is also reflected in preview HTML/CSS.

## Fonts

Source: [Noto Sans CJK SC Regular](https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf),
SIL Open Font License 1.1. The license is preserved in `OFL-NotoSansCJK.txt`.
Source font copyright: © 2014-2021 Adobe (http://www.adobe.com/).
This notice is extracted from the actual source OTF and preserved with its license.
The Luna-named bitmap derivatives are generated with `lv_font_conv@1.5.3`.

The 16/28 px fonts contain all 31,031 selected source-mapped characters:
ASCII/Latin-1, punctuation, Kana/Bopomofo, Han and supplementary/compatibility Han.
This is not a claim to cover every Unicode character or other writing system.
Small UI fonts contain all static labels. Dynamic music titles/artists/locations
use the full fonts. Line boxes follow the preview's 1.5em spacing while preserving
bitmap and baseline. Very rare tall glyphs can extend outside that line box.

Full 16/28 px fonts are compressed and use the project-owned, serialized bitmap
decoder to protect LVGL 9.3 global RLE state across draw workers. Static UI fonts
and 86/101 px digits are uncompressed for cheaper redraw. The manifest records
compression per font. Call `luna_font_decode_init` before first UI rendering.

The web preview uses Windows-installed Segoe UI/Microsoft YaHei. Those fonts are
not bundled. Noto is the redistributable device font, so typography is not claimed
to be pixel-identical to Windows rendering.

## Reproduction

1. Install `lv_font_conv@1.5.3` under ignored `.tools/lv-font-conv`.
2. Put the source OTF there, named `NotoSansCJKsc-Regular.otf`. Its SHA256 must be
   `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`.
3. Supply `sharp` via `NODE_PATH` and run `node scripts/generate-luna-ui-assets.cjs`.

`manifest.json` records character coverage and output hashes. Generated C files
are checked in; ordinary firmware builds do not need the generator or source OTF.

Full fonts make B1 larger than 6 MiB. Its isolated partition table allocates
10 MiB to the app using only the retired speech-model gap. NVS and storage/TF
locations are unchanged; do not erase flash to install this profile.
