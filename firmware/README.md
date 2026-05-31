# Firmware — AirBox

Arduino sketch for the Adafruit QT Py ESP32-S3 standalone environmental
monitor. Reads a BME688 (via Bosch BSEC2) and an HDC3022, serves a local web
dashboard, and provisions WiFi through a captive portal.

## Board

- **Adafruit QT Py ESP32-S3** (No PSRAM or 2MB PSRAM — both work)
- Arduino IDE board: **Tools → Board → ESP32 Arduino → Adafruit QT Py ESP32-S3**
- Install the **esp32 by Espressif Systems** board package (arduino-esp32 v3.x)
- Partition scheme: **REQUIRED → "Minimal SPIFFS (1.9MB APP with OTA/128KB
  SPIFFS)"** (Tools → Partition Scheme). This is mandatory, not optional:
  - The QT Py S3's **default partition has _no_ OTA and no usable filesystem**,
    which would silently break **browser OTA** *and* **persistent history**.
  - "Minimal SPIFFS" provides the two app slots OTA needs **and** a small
    filesystem for the saved trend history.
  - arduino-cli: append it to the FQBN —
    `…:adafruit_qtpy_esp32s3_n4r2:PartitionScheme=min_spiffs`
  - Trade-off: this replaces the TinyUF2 layout, so drag-and-drop `.uf2`
    flashing is unavailable. Flash over USB/serial (or OTA) instead.

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

## Building an OTA binary

The browser updater at `/update` takes a compiled **app image** (`*.ino.bin`).
Two ways to produce it:

**Arduino IDE 2.x**
1. Select the board **and** Partition Scheme **"Minimal SPIFFS (1.9MB APP with
   OTA/128KB SPIFFS)"** — this is required; a non-OTA scheme produces a binary
   that uploads but won't boot.
2. **Sketch → Export Compiled Binary** (`⌘⌥S`).
3. The binaries land in a `build/<fqbn>/` folder inside the sketch directory.
   Upload **`airbox.ino.bin`** to `/update`.
   - Do *not* use `airbox.ino.merged.bin`, `*.bootloader.bin`, or
     `*.partitions.bin` — those are for a full USB/esptool flash, not OTA.

**arduino-cli**
```sh
arduino-cli compile \
  --fqbn "esp32:esp32:adafruit_qtpy_esp32s3_n4r2:PartitionScheme=min_spiffs" \
  --output-dir build  firmware/airbox
# -> upload build/airbox.ino.bin at http://<hostname>.local/update
```
(Use `…_nopsram` instead of `…_n4r2` for the no-PSRAM board.)

Bump `FW_VERSION` in `config.h` before building so the OLED header / dashboard
confirm the new build actually took after the OTA.

## Notes on the preserved sensor core

The BSEC2 integration, CRC32-wrapped NVS calibration persistence, per-sensor
health/staleness/reinit logic, and the 60 s task watchdog are carried over
unchanged from the Home Assistant version. The watchdog is automatically
extended to 5 minutes during an OTA flash and restored afterward. BSEC
calibration (NVS namespace `bsec`) is **preserved across a WiFi reset** — only
the `cfg` namespace is cleared when you reconfigure WiFi.
