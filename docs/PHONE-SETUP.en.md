# Set up Luna from a phone

The **2026.11.27 release** includes this phone setup flow. Older firmware without a phone portal must be upgraded first; see [deployment](DEPLOYMENT.en.md). Clock/weather/Focus do not need a Windows Agent.

## Clock and weather: no PC script needed

1. Power Luna. With no saved network, setup opens automatically. Otherwise tap **Wi-Fi** on the weather card.
2. Connect your phone to `Luna-Setup-…` directly: **no hotspot password is required**. Stay connected if the phone reports no Internet.
3. Open **http://192.168.4.1**. Automatic captive-portal opening is not guaranteed. Use HTTP, not HTTPS.
4. Tap **扫描 Wi-Fi** (scan), select your **2.4 GHz Wi-Fi**, enter its password and tap **连接并继续** (connect). Selecting alone never joins; changing the selection clears the previous password. Manual name entry remains available for hidden networks or failed scans. This only tries the network in RAM; saved settings remain intact.
5. After Luna connects and synchronizes network time, search for a city and verify its region/country, or enter a name and explicit coordinates manually. Press **Save to Luna**. Luna performs the city query; the phone need not have Internet access.

Scan results appear as selectable rows showing the network name, signal and password requirement. Connection and save results appear beside their respective buttons and scroll into view when changed. Interrupted requests remain unconfirmed until device status resolves them; do not resubmit blindly.

While the device setup dialog is visible, background taps, swipes and standby entry are blocked. **暂时隐藏** restores card interaction while leaving the hotspot open; **关闭热点** ends setup.

Saved settings, Wi-Fi connectivity and fresh weather are separate results. Correct a wrong password by trying again. The hotspot closes about 20 seconds after saving and connecting, or after 10 minutes otherwise. The Chinese buttons **暂时隐藏** (hide) and **关闭热点** (close hotspot) respectively hide the dialog and end setup. Cancel/timeout restores the last committed network or disconnects an unsaved first-install trial. Check the hotspot name on the screen when reopening.

Settings persist in NVS. Weather and NTP time run on Luna without a PC; the timezone remains UTC+8. Reconnect your phone to its normal network afterward.

## PC features: use the Windows Agent

Music/system volume, CPU/GPU/memory, VS Code projects and Codex quota need PC sources. Pair Luna manually in Windows, install Python 3.11 x64 and run **Start-Luna.cmd** as described in deployment. It installs a current-user login resident and no longer prompts for weather. Skip it if you only need clock/weather.

`Configure-Weather.cmd` remains an optional authenticated BLE fallback, described in the [BLE guide](WEATHER-SETUP.en.md); it is not required for phone setup.

## Updating and troubleshooting

- Existing Luna with the same layout: flash the app at **0x20000**, preserving pairing/NVS. Merged **0x0** images are only for first installations permitted to clear network, bonds and weather data. Do not reflash C6 automatically.
- No hotspot: keep DC power connected and tap Wi-Fi. Report the Chinese “Wi-Fi 初始化失败，错误码 …” (Wi-Fi initialization failed) to the maintainer: the existing C6 may lack compatible Hosted Wi-Fi RPC.
- Phone leaves the hotspot: choose “stay connected/use without Internet.” Luna provides no NAT or Internet sharing.
- Empty/failed scan: wait a few seconds and scan again, or enter the name manually. Hidden networks require manual entry; scanning may be unavailable while the station is connecting. Existing settings are retained.
- Search fails: wait for network/NTP, try pinyin or enter coordinates manually. The device network must reach DNS, NTP and Open-Meteo HTTPS.
- Disconnected after saving: inspect Luna and query status after reconnecting; do not repeatedly submit. Successful hotspot shutdown is expected.
- Ordinary network outages do not silently reopen setup. Re-enter from the physical screen without deleting NVS or re-pairing.

Scanning runs locally on Luna. At most 16 networks are shown, deduplicated by name and open/secured status; only SSID, signal strength and password-required status are returned, without BSSID/MAC or forwarding to PC/city services. An open network is not necessarily trusted.

## Privacy

As explicitly requested, the setup hotspot is open with no password. It allows one phone and closes after ten minutes or earlier after successful setup. HTTP accepts only the AP destination and checks Host/Origin/session tokens. No setup page is exposed on the home LAN; no CORS, DNS interception, NAT, CDN, localStorage, account or new cloud relay is used.

Credentials are not sent to city services, echoed in status, or saved to PC files/logs. The open hotspot and local HTTP do not encrypt the wireless transport; nearby devices may join or intercept traffic. Open setup only when needed. Host/Origin/token checks prevent cross-page writes, not wireless interception. Flash/NVS encryption at rest is not claimed: do not share device flash dumps.

Luna queries [Open-Meteo Geocoding](https://open-meteo.com/en/docs/geocoding-api) over HTTPS, using [GeoNames](https://www.geonames.org/) data. Transport uses existing [ESP-Hosted AP+STA](https://github.com/espressif/esp-hosted-mcu/tree/main/examples/wifi/apsta) and ESP-IDF HTTP Server. Each C6 firmware combination still requires device validation.
