# First-time weather setup

For phone-first `2026.11.28`, use [phone hotspot setup](PHONE-SETUP.en.md). Clock/weather need no PC script. This page retains the optional BLE fallback; normal Start-Luna no longer asks a weather question.

The current **2026.11.28** firmware retains this authenticated BLE setup fallback; the older `v2026.11.25-preview` does not support it. Normal setup uses the phone portal. Verification scope is recorded in the release manifest.

1. Flash the new app at **0x20000** on an existing Luna with the same partition layout to preserve pairing. Use the new **0x0** merged image only for first installation: it clears pairing, Wi-Fi settings and weather cache. Do not erase the whole chip or flash C6. Keep DC power connected and manually pair Luna in Windows, confirming matching numbers on both screens.
2. For this optional BLE fallback, install Python 3.11 x64, extract the Windows package to a permanent folder and run **Configure-Weather.cmd**. Weather-only setup does not install or start a previously stopped resident; it temporarily pauses/resumes only the matching resident already running from this folder. Phone setup does not need this script.
3. Search for a city and choose the correct region/country, then enter the **2.4 GHz Wi-Fi** SSID and password. The password is hidden. Confirm to save settings to Luna. The wizard reports saved, online and fresh-weather states separately. No API key is needed.

The device fetches weather directly over HTTPS and retains settings across power cycles. If city search fails, use an English name or enter `m` for an explicit display name, latitude and longitude. Enter `q` to cancel. After an uncertain write/timeout, reconnect and read existing settings before making changes; writes are never automatically replayed.

City queries go to [Open-Meteo Geocoding](https://open-meteo.com/en/docs/geocoding-api), using data from [GeoNames](https://www.geonames.org/). Wi-Fi credentials are not sent there. They travel over the existing paired/authenticated BLE channel, are not CLI arguments or PC files, and are never echoed by device replies. P4 NVS stores them; this build does not claim encryption at rest. Do not share device flash dumps. Open-Meteo data/service [terms and attribution](https://open-meteo.com/en/terms) apply.

Saved settings do not prove connectivity: a wrong password, incompatible existing C6 Wi-Fi firmware, network restrictions or HTTPS/time errors may still prevent live weather. The device retries. Do not flash C6 automatically. The timezone remains UTC+8. See the [Chinese troubleshooting guide](WEATHER-SETUP.md) and [deployment guide](https://github.com/2823700739-sudo/ESP32-P4-4B-Luna/blob/main/docs/DEPLOYMENT.en.md).
