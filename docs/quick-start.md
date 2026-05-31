# AirBox — Quick Start

> 🖨️ **Printable one-pager:** [`AirBox-Quick-Start.pdf`](AirBox-Quick-Start.pdf)
> (includes a scan-to-open QR for this repo). Regenerate with
> [`make-quickstart-pdf.py`](make-quickstart-pdf.py).

A standalone indoor air & environment monitor. **No app, account, or cloud** —
it serves its own dashboard on your local network.

**You'll need:** USB-C power, and a **2.4 GHz** WiFi network (name + password).

---

## 1. Power it on
Plug in USB-C. The little screen shows **WiFi Setup**, the network name
**`AirBox-Setup`**, and a **QR code**.

## 2. Join the device
On your phone, **point the camera at the QR code** → tap to join the
**`AirBox-Setup`** WiFi (it's open, no password). A setup page opens
automatically. *(If it doesn't, open a browser to `http://192.168.4.1`.)*

## 3. Hand it your WiFi
Pick your network from the list, enter its password, tap **Connect**. The device
reboots and joins your WiFi. The screen then shows its address.

## 4. Open the dashboard
On any device on the **same WiFi**, browse to:

> ## http://airbox.local

*(If `.local` doesn't resolve on your network, use the IP address shown on the
device's screen.)*

---

## Reading it
Four live tiles — **Temperature, Humidity, Pressure, Air Quality (IAQ)** — each
with a 24-hour trend chart.
- **IAQ** is a 0–500 index (lower = cleaner). It needs **24–48 h** of run time to
  self-calibrate; "calibration" climbs from 0 to 3 on the Diagnostics tab.
- **Diagnostics** tab: sensor health, network info, system stats, and a
  **Download CSV** button (last 7 days of data).
- **Settings** tab: device name, °F/°C, time zone, screen brightness & night
  mode, and the admin password.

## Good to know
- **Admin password** (for Settings changes & firmware updates) defaults to
  **`airbox`** — change it under Settings → Security.
- **Firmware updates** happen in the browser at **`http://airbox.local/update`**.
- The screen dims/sleeps on a schedule if you enable **Night Mode**; the
  dashboard always works even when the screen is off.

## If something seems off
- **Can't reach `airbox.local`** → use the **IP address** shown on the screen.
- **Changing WiFi / moved it to a new network** → on the dashboard,
  **Settings → Reconfigure WiFi**, or hold the internal **BOOT** button for ~3 s
  to return to the setup screen. (Your air-quality calibration is preserved.)
- **Screen shows `STALE`** → a momentary sensor hiccup; it self-recovers, or just
  power-cycle it.

---

**Full guide, data details & source code:**
[github.com/FrozenSection/airbox](https://github.com/FrozenSection/airbox)
