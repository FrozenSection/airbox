# Using the dashboard

Open `http://airbox.local` on any device on the same WiFi network. The page has
three tabs: **Dashboard**, **Diagnostics**, **Settings**.

## Dashboard tab

### Comfort verdict
At the top, a plain-language **verdict** summarizes the room from the temperature
and humidity against your **Comfort targets** (set in Settings) — e.g.
*Comfortable, Cool, Warm, Dry, Humid, Cold & dry, Warm & humid…* — with a
one-line suggestion (e.g. "the air is dry — a humidifier would help"). The page's
accent tint shifts with it. It's an interpretation layered on top of the numbers;
the raw readings are always shown below.

### The four readings
Each with an 8-hour trend chart:

| Reading | Source | Notes |
|---|---|---|
| **Temperature** | HDC3022 | Primary, lab-grade (±0.1 °C). Shown in your chosen °F/°C. |
| **Humidity** | HDC3022 | Primary (±0.5 % RH). |
| **Pressure** | BME688 | Barometric pressure (hPa). Good for spotting weather trends. |
| **Air Quality (IAQ)** | BME688 + BSEC2 | 0 = excellent … 500 = very poor. See calibration note below. |

The **Temperature** and **Humidity** tiles also show a **range bar** marking where
the current value sits within your ideal band (`ideal X–Y`).

Charts show the most recent **~8 hours** (a sample every 5 minutes) with a time
axis (`-Xh … now`) and **min/max scale labels** so you can read the actual range
at a glance. History is saved to flash, so it **survives a reboot** and keeps up
to **7 days** for CSV export (Diagnostics tab).

> **A note on sudden steps in the trend.** Each chart point is a snapshot taken
> every 5 minutes, so a *fast* real-world change — opening a window, cooking, a
> shower in the next room — can appear as a steep, near-vertical step at the right
> edge rather than a smooth curve. That's expected: the chart is a 5-minute-
> resolution **trend**, and it simply has no in-between points to draw a fast
> event with. The big numbers at the top of each tile are the live reading (they
> update every few seconds); the chart will catch up and smooth out as the next
> few samples come in. Nothing is wrong with the sensor.

## Diagnostics tab

The raw/secondary values and device health, grouped by subsystem:

- **Sensors** — each sensor's status (OK/fault), seconds since its last good
  reading, and the BME688's **calibration** state (0–3).
- **Air quality detail** — **raw BME688 temperature / humidity** (these read a
  bit high because the gas heater warms the chip — trust the HDC3022 on the
  Dashboard for ambient T/RH), plus **eCO₂ / bVOC**, which are BSEC2 *estimates*
  from the same gas signal as IAQ (they track IAQ rather than adding independent
  info).
- **Network** — IP address, hostname, WiFi SSID, signal strength (RSSI).
- **System** — firmware version, uptime, free heap, and **last reset reason**
  (Power-on / Brownout / Task WDT / Software / Panic — handy for diagnosing an
  unexpected restart).
- **Export data (CSV)** — **Download CSV** grabs the last 7 days at 5-minute
  spacing (timestamp, temperature, humidity, pressure, IAQ).

## Settings tab

Grouped into sections:

- **Device** — Device Name (header / OLED / browser title), Temperature Unit
  (°F/°C), mDNS Hostname (the `.local` address; applies after restart), and
  **Time Zone** (a named zone — DST is handled automatically; used for
  timestamps and the night-mode clock, synced over NTP).
- **Comfort targets** — the ideal **temperature** and **humidity** bands that
  drive the verdict and range bars, entered in your chosen unit. Applies on save.
- **Display** — **Brightness** (Low / Medium / High / Max OLED contrast; lower
  meaningfully extends panel life, default Medium) and **Night Mode** (between
  the hours you set, the OLED either **blanks** or **dims** — your choice; the
  sub-options grey out when it's off). The dashboard and sensors keep running
  even while the screen is dark.
- **Maintenance** — Firmware update (browser OTA), Recalibrate air sensor
  (clears the BSEC baseline → IAQ accuracy 0), Reconfigure WiFi, Restart (the
  last two: see [recovery.md](recovery.md)).

## About IAQ calibration

The **calibration** indicator (Diagnostics tab) climbs from *unreliable* → *low*
→ *medium* → *high* over the first 24–48 hours as BSEC2 learns your environment's
baseline. Until it reaches *medium/high*, treat IAQ as a relative trend rather
than an absolute number. The learned calibration is saved to flash and restored
on reboot, so this only happens once (unless you recalibrate).

## Air-quality reference

The IAQ tile shows the **descriptor** for the current level (with a green→red dot)
and the **Air quality note** card explains it and suggests what to do. The bands
are Bosch's BSEC IAQ classification:

| IAQ | Descriptor |
|---|---|
| 0–50 | Excellent |
| 51–100 | Good |
| 101–150 | Lightly polluted |
| 151–200 | Moderately polluted |
| 201–250 | Heavily polluted |
| 251–350 | Severely polluted |
| 351–500 | Extremely polluted |
