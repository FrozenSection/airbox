# First-time setup

When the device has no saved WiFi network (fresh flash, or after a WiFi reset)
it starts a temporary setup hotspot so you can tell it which network to join.

## The quick way (with the OLED)

1. **Power the device.** After a few seconds the OLED shows **"WiFi Setup"**,
   the hotspot name **`AirBox-Setup`**, and a **QR code**.
2. **Point your phone camera at the QR code.** It offers to join the
   `AirBox-Setup` network — tap to join. (The setup hotspot is open/passwordless
   on purpose, so the QR stays crisp on the small screen. It only ever serves
   the setup page.)
3. Your phone should **automatically pop up the setup page**. If it doesn't,
   open a browser to **`http://192.168.4.1`**.
4. **Pick your home WiFi** from the list, type its password, tap **Connect**.
5. The device reboots and joins your network. The OLED then shows
   **"Connected!"** with the dashboard address and a QR code linking to it.

## The manual way (no OLED, or QR won't scan)

1. On your phone or laptop, open WiFi settings and join the network
   **`AirBox-Setup`**.
2. A captive-portal page should appear automatically. If not, browse to
   **`http://192.168.4.1`**.
3. Select your network, enter the password, **Connect**.

## After setup

- Open the dashboard at **`http://airbox.local`** (or scan the QR shown on the
  OLED, or use the IP address your router assigned).
- Go to the **Settings** tab and:
  - set a **device name** (e.g. "Office", "Nursery"),
  - choose **°F or °C**,
  - **change the admin password** (default is `airbox`) — this protects
    firmware updates and settings changes.

> **Air-quality calibration:** the IAQ reading starts at accuracy 0 and
> self-calibrates over the first 24–48 hours of normal use. This is expected;
> see [dashboard.md](dashboard.md).

## Acceptance checklist

- [ ] Fresh boot shows the `AirBox-Setup` hotspot (and QR on the OLED).
- [ ] Phone scans the QR, joins, and the setup page appears automatically.
- [ ] Network list populates; selecting your WiFi and submitting reboots the
      device onto your network.
- [ ] `http://airbox.local` loads the dashboard; live values update.
- [ ] Trend charts begin filling in (first points appear after a few minutes).
- [ ] °F/°C toggle and device name persist across a reboot.
- [ ] Browser OTA at `/update` accepts the admin password and flashes a new
      build, after which the device reboots on the new firmware.
- [ ] Holding **BOOT** for ~3 s returns the device to the setup portal, and the
      IAQ calibration is still intact afterward.
