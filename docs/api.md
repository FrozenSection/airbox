# Terminal / HTTP API

AirBox has no login shell — it's a microcontroller — but it exposes a clean HTTP
API over WiFi plus a USB serial console. Everything the dashboard does, you can do
from a terminal with `curl`.

The API is **unauthenticated by design** — a trusted-LAN environmental monitor.
Anyone on the same WiFi can read and control it. (USB and OTA flashing are the only
ways to change firmware.)

mDNS resolves `airbox.local`, but the resolver lags a few seconds — for scripting,
use the device's raw IP (shown on the OLED, or read `.ip` from `/api/data`).

---

## 1. Over WiFi — `curl` against the HTTP API

### Read live telemetry (JSON)

```bash
curl -s http://airbox.local/api/data | jq
```

This is the same payload the dashboard polls. Key fields:

| Field | Meaning |
|---|---|
| `temp`, `rh` | Temperature (in the configured unit) and humidity % — HDC3022 |
| `pressure` | Barometric pressure (hPa) — BME688 |
| `iaq`, `iaq_acc` | Air-quality index (0–500) and BSEC accuracy (0–3) |
| `eco2`, `bvoc` | Estimated CO₂ (ppm) and breath-VOC (ppm) — BME688 |
| `bme_temp`, `bme_rh` | BME688's own temp/RH (secondary; HDC3022 is primary) |
| `hdc_ok`, `bme_ok` | Per-sensor health flags |
| `rssi`, `ssid`, `chan` | WiFi signal (dBm), network, channel |
| `mac`, `bssid`, `ip` | Device MAC (for a DHCP reservation), connected AP/mesh node, IP |
| `uptime` | Seconds since boot |
| `heap`, `heap_min`, `heap_largest` | Free / min-ever / largest-contiguous internal RAM (bytes) |
| `psram_free` | Free PSRAM (bytes) |
| `chip_temp` | ESP32-S3 die temperature (°C) — runs warm, ~50–70 °C is normal |
| `reset_reason` | Why it last rebooted |
| `fw` | Firmware version (confirms an OTA took) |

Pull one field:

```bash
curl -s http://airbox.local/api/data | jq '.chip_temp, .rssi'
```

### Watch it live (1 Hz)

```bash
while true; do curl -s http://airbox.local/api/data \
  | jq -c '{t:.temp, rh, iaq, chip:.chip_temp, rssi}'; sleep 1; done
```

### History

```bash
curl -s http://airbox.local/api/history      | jq   # JSON, the 8 h chart window
curl -s http://airbox.local/api/history.csv  -o airbox-history.csv  # full 7 days, streamed
```

### Actions (POST)

```bash
curl -X POST http://airbox.local/api/restart                          # reboot
curl -X POST http://airbox.local/api/recalibrate                      # kick BSEC recalibration
curl -X POST http://airbox.local/api/clear-history                    # wipe trend log only
curl -X POST http://airbox.local/api/factory-reset -d 'wifi=1&calib=1'
```

For `factory-reset`, each flag is optional and defaults off (`0` = keep):
- `wifi=1`  — also forget the WiFi credentials (forces re-provisioning)
- `calib=1` — also discard the BSEC calibration state

### Change settings (POST, form-encoded)

```bash
curl -X POST http://airbox.local/api/settings \
  -d 'name=AirBox&unit=F&hostname=airbox&brightness=64&tz=1&night_en=0'
```

Accepted fields (send only what you're changing):

`name`, `unit` (`F`/`C`), `hostname`, `brightness` (0–255), `night_en` (0/1),
`night_start`, `night_end` (minutes-of-day), `night_mode` (0=blank, 1=dim),
`tz` (timezone index), `comfort_tmin`, `comfort_tmax`, `comfort_hmin`,
`comfort_hmax`, `comfort_unit`.

Changes take effect on the next firmware loop (a flag is set; the loop applies it).

### Firmware update (OTA)

Browser: `http://airbox.local/update` (ElegantOTA). Or scripted:

```bash
curl -F 'firmware=@airbox.ino.bin' http://airbox.local/update
```

### Endpoint summary

| Method | Path | Purpose |
|---|---|---|
| GET  | `/` | Dashboard (gzipped HTML) |
| GET  | `/favicon.ico` | Tab icon |
| GET  | `/api/data` | Live telemetry (JSON) |
| GET  | `/api/history` | 8 h chart window (JSON) |
| GET  | `/api/history.csv` | Full 7-day history (CSV, chunked) |
| POST | `/api/settings` | Update configuration |
| POST | `/api/restart` | Reboot |
| POST | `/api/recalibrate` | Kick BSEC recalibration |
| POST | `/api/reconfigure` | Re-run first-time setup logic |
| POST | `/api/clear-history` | Wipe the trend log |
| POST | `/api/factory-reset` | Reset config (optional `wifi`/`calib` flags) |
| *  | `/update` | ElegantOTA firmware upload |

A handy alias for daily use:

```bash
alias airbox='curl -s http://airbox.local/api/data | jq'
```

---

## 2. Over USB — serial console

For boot logs, the WiFi-join sequence, sensor init, and low-level diagnostics —
especially useful when WiFi won't come up and the HTTP API is unreachable.

Plug in the QT Py and open the console at **115200 baud**:

```bash
ls /dev/cu.usb*                              # find the port
screen /dev/cu.usbmodemXXXX 115200           # or: minicom -b 115200 -D <port>
```

Exit `screen` with `Ctrl-A` then `K`.

---

See also: [dashboard.md](dashboard.md) for the web UI, and
[recovery.md](recovery.md) for stuck-device procedures.
