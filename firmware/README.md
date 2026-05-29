# Firmware — AirBox

Arduino sketch for the Adafruit QT Py ESP32-S3 standalone environmental
monitor. Reads a BME688 (via Bosch BSEC2) and an HDC3022, serves a local web
dashboard, and provisions WiFi through a captive portal.

## Board

- **Adafruit QT Py ESP32-S3** (No PSRAM or 2MB PSRAM — both work)
- Arduino IDE board: **Tools → Board → ESP32 Arduino → Adafruit QT Py ESP32-S3**
- Install the **esp32 by Espressif Systems** board package (arduino-esp32 v3.x)
- Partition scheme: **Minimal SPIFFS (1.9MB APP with OTA)** or any layout with
  an OTA app partition (web OTA needs two app slots). The default app layout
  also works; "Minimal SPIFFS" is what the original was verified on.

## Libraries (Arduino IDE → Library Manager)

| Library | Author | Notes |
|---|---|---|
| Adafruit SSD1306 | Adafruit | OLED (+ Adafruit GFX, Adafruit BusIO pulled in) |
| Adafruit HDC302x | Adafruit | HDC3022 temperature/humidity |
| BSEC2 Software Library | Bosch Sensortec | BME688 air quality (`bsec2`) |
| ArduinoJson | Benoit Blanchon | v7.x |
| **ESP Async WebServer** | **ESP32Async** | the maintained fork — *not* me-no-dev |
| **Async TCP** | **ESP32Async** | matching fork; install explicitly |
| **ElegantOTA** | **Ayush Sharma** | v3.x, browser OTA |
| PubSubClient | Nick O'Leary | only if `ENABLE_MQTT` is set |

> Install the **ESP32Async** versions of ESP Async WebServer / Async TCP. The
> older `me-no-dev` packages are incompatible with arduino-esp32 v3.x. Pin the
> versions once the sketch builds cleanly.

`DNSServer`, `ESPmDNS`, `Update`, `Preferences`, `WiFi`, `Wire`, and the QR
encoder (`qrcode.h`, used for the OLED setup/URL codes) all ship with the ESP32
core — no separate install. (Don't install the ricmoo "QRCode" library — its
header name collides with the core's and would break the build.)

### ElegantOTA async mode — handled automatically

ElegantOTA must be told to use AsyncWebServer. A committed
[`airbox/build_opt.h`](airbox/build_opt.h) injects
`-DELEGANTOTA_USE_ASYNC_WEBSERVER=1` globally, which both Arduino IDE 2.x and
arduino-cli honor — so there's nothing to configure and no library files to
edit. (Verified: compiles clean on esp32:esp32 3.3.8 for the QT Py S3.)

## Build flags (`config.h`)

Everything tunable lives in [`airbox/config.h`](airbox/config.h).
No secrets are stored in source — WiFi credentials, the admin/OTA password,
and optional MQTT settings are entered in the web UI and saved to NVS.

- `ENABLE_MQTT` (default `0`) — also push to an MQTT broker / Home Assistant.
- `ENABLE_ARDUINO_OTA` (default `0`) — network flashing from the IDE/PlatformIO.
- `DEFAULT_ADMIN_PASS` (`"airbox"`) — initial password for OTA + Settings.
  **Change it in the dashboard Settings page after first boot.**

## First flash

1. Open `airbox/airbox.ino` in Arduino IDE.
2. Select the board + partition scheme above, install the libraries.
3. Upload over USB-C.
4. On first boot the device has no WiFi credentials → it starts the
   **AirBox-Setup** access point. Follow [docs/first-time-setup.md](../docs/first-time-setup.md).

After the first flash, firmware updates can be done entirely in the browser at
`http://<hostname>.local/update` (no cable needed).

## Notes on the preserved sensor core

The BSEC2 integration, CRC32-wrapped NVS calibration persistence, per-sensor
health/staleness/reinit logic, and the 60 s task watchdog are carried over
unchanged from the Home Assistant version. The watchdog is automatically
extended to 5 minutes during an OTA flash and restored afterward. BSEC
calibration (NVS namespace `bsec`) is **preserved across a WiFi reset** — only
the `cfg` namespace is cleared when you reconfigure WiFi.
