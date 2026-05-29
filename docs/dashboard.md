# Using the dashboard

Open `http://airbox.local` on any device on the same WiFi network. The page has
three tabs.

## Dashboard tab

The four primary readings, each with a 24-hour trend chart:

| Reading | Source | Notes |
|---|---|---|
| **Temperature** | HDC3022 | Primary, lab-grade (±0.1 °C). Shown in your chosen °F/°C. |
| **Humidity** | HDC3022 | Primary (±0.5 % RH). |
| **Pressure** | BME688 | Barometric pressure (hPa). Great for spotting weather trends. |
| **Air Quality (IAQ)** | BME688 + BSEC2 | 0 = excellent … 500 = very poor. See calibration note below. |

Charts hold ~24 hours of history (a sample every 5 minutes). They live in the
device's RAM, so they reset on reboot and fill back in over time.

## Diagnostics tab

The less-essential and raw values, kept out of the main view:

- **Raw BME688 temperature / humidity** — these read a bit high because the
  BME688's gas heater warms the chip. Trust the HDC3022 on the Dashboard tab for
  ambient T/RH; the raw values are here for reference.
- **eCO₂ / bVOC** — BSEC2 *estimates* derived from the same gas signal that
  drives IAQ, so they track IAQ closely rather than adding independent
  information. Shown for completeness.
- **WiFi RSSI, uptime, free heap** — device health.
- **Sensor status & reading age** — each sensor reports OK/fault and seconds
  since its last good reading, so a flaky connection is visible.
- **Firmware version.**

## Settings tab

- **Device name** — shown in the header, on the OLED, and in the browser title.
- **Temperature unit** — °F or °C (applies to the live readings and charts).
- **mDNS hostname** — changes the `.local` address (applies after a restart).
- **Display brightness** — Low / Medium / High / Max OLED contrast. Lower is
  gentler on the panel and meaningfully extends its life; the default is Medium.
- **Night mode** — between the hours you set (e.g. 23 → 7) the OLED either
  **turns fully off (blank)** or just **dims** — your choice in Settings; blank
  is the default and saves the most panel life. Needs the **UTC offset** set so
  the device knows the local time (it syncs the clock over the internet via
  NTP). Leave night mode off to keep the screen always on. The dashboard and
  sensors keep running regardless of the screen state, so you can always check
  data or change settings from the web portal even while the screen is dark.
- **Admin password** — protects firmware updates and settings. Leave blank to
  keep the current one. **Change the default (`airbox`) after first setup.**
- **Firmware update** — opens the browser OTA uploader.
- **Recalibrate air sensor** — clears the BSEC baseline (IAQ accuracy → 0).
- **Reconfigure WiFi** / **Restart** — see [recovery.md](recovery.md).

## About IAQ calibration

The **calibration** indicator under the IAQ reading climbs from *unreliable* →
*low* → *medium* → *high* over the first 24–48 hours as BSEC2 learns your
environment's baseline. Until it reaches *medium/high*, treat IAQ as a relative
trend rather than an absolute number. The learned calibration is saved to flash
and restored on reboot, so this only happens once (unless you recalibrate).

## Air-quality reference (rough)

| IAQ | Meaning |
|---|---|
| 0–50 | Excellent |
| 51–100 | Good |
| 101–150 | Lightly polluted |
| 151–200 | Moderately polluted |
| 201–300 | Heavily polluted |
| 301–500 | Severely polluted — ventilate |
