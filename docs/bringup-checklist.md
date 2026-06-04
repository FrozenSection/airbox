# AirBox bring-up checklist

Step-by-step for when the parts arrive: assemble → flash → provision → verify →
burn-in → prep for gifting. Check the boxes as you go.

The firmware **compiles clean** (verified with arduino-cli, esp32:esp32 3.3.8,
QT Py S3). What's left is hardware-dependent and can only be confirmed on the
real device.

---

## 1. Assemble the hardware

- [ ] Daisy-chain over STEMMA QT (the QT Py's second I²C bus, `Wire1`):
      **QT Py ESP32-S3 → HDC3022 → BME688 → SSD1306 OLED** (order doesn't matter
      electrically; all are on the same bus).
- [ ] Confirm I²C addresses don't collide: HDC3022 `0x44`, BME688 `0x77`,
      OLED `0x3C` or `0x3D`. (All different — fine.)
- [ ] In the enclosure: vents near the BME688, BME688 set a little apart from
      the ESP32-S3 (both self-heat), OLED window, USB-C access. BOOT button stays
      *inside* (it's the deliberate factory-reset path — no external button).

## 2. Toolchain (this Mac is already set up)

Already installed on this machine: `arduino-cli`, esp32 core 3.3.8, and the
libraries. To rebuild the toolchain elsewhere, see
[`firmware/README.md`](../firmware/README.md). Confirm the board variant:

- [ ] **No-PSRAM** board → base FQBN `esp32:esp32:adafruit_qtpy_esp32s3_nopsram`
- [ ] **2MB PSRAM** board → base FQBN `esp32:esp32:adafruit_qtpy_esp32s3_n4r2`
- [ ] **Partition scheme = "Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)"** —
      **required.** The board default (`tinyuf2_noota`) has *no* OTA slot and no
      filesystem, which breaks browser OTA *and* persistent history. With
      arduino-cli, append `:PartitionScheme=min_spiffs` to the FQBN (below).

## 3. Compile & flash

```sh
cd ~/Documents/GitHub/airbox
# include the partition scheme in the FQBN (use _nopsram for the no-PSRAM board)
FQBN="esp32:esp32:adafruit_qtpy_esp32s3_n4r2:PartitionScheme=min_spiffs"

arduino-cli compile --fqbn "$FQBN" firmware/airbox        # ~66% flash
arduino-cli board list                                    # find the /dev/cu.usbmodem* port
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn "$FQBN" firmware/airbox
arduino-cli monitor -p /dev/cu.usbmodemXXXX -c baudrate=115200   # watch boot logs
```

> Changing the partition scheme repartitions the chip — stored WiFi credentials
> and BSEC calibration **may be wiped**, so re-provision if it boots into the
> setup portal after this flash.

- [ ] Compiles without errors.
- [ ] Uploads. (If upload fails: **double-tap RESET** to force the S3 bootloader,
      then retry. The port name may change in bootloader mode.)
- [ ] Serial monitor shows `AirBox v1.0.0 booting...` and the sensor init lines
      (`OLED detected`, `BME688/BSEC initialized`, `HDC3022` ok). If a sensor
      reports MISSING, recheck that STEMMA QT link.

## 4. First boot — WiFi provisioning

- [ ] OLED shows **"WiFi Setup"**, `AirBox-Setup`, `192.168.4.1`, and a **QR**.
- [ ] Phone camera scans the QR → offers to join `AirBox-Setup` → join it.
- [ ] Captive page pops automatically (if not, browse to `http://192.168.4.1`).
- [ ] Network list populates; pick your WiFi, enter password, **Connect**.
- [ ] Device reboots, joins, and the OLED shows **"Connected!"** with
      `airbox.local`, the IP, and a dashboard QR.

> Test this on **both** an iPhone and an Android if you have them handy — the
> captive-portal auto-pop behaves slightly differently per OS, and it's the one
> flow only real hardware can confirm.

## 5. Dashboard

- [ ] Open **`http://airbox.local`** (and the raw IP as a fallback) — dashboard loads.
- [ ] Live values update every few seconds (temp, humidity, pressure, IAQ).
- [ ] Trend charts start drawing after a few minutes; fuller after ~an hour.
- [ ] **Diagnostics** tab shows raw BME, eCO₂/bVOC, RSSI, uptime, heap, sensor
      health, ages, firmware version.
- [ ] **Settings** tab:
  - [ ] Set a **device name**; confirm it shows in the header, OLED, and tab title.
  - [ ] Toggle **°F/°C**; confirm readings + charts switch and it survives a reboot.

## 6. OLED longevity settings

- [ ] **Brightness**: try Low/Medium/High; confirm the panel visibly changes.
      Leave it where it looks good (Medium is a sensible gift default).
- [ ] **Night mode**: set your **UTC offset** first (so the clock is right), then
      enable night mode with an off-window (e.g. 23 → 7) and **blank** behavior.
      Easiest test: temporarily set the window to *now* and confirm the screen
      goes dark, then revert. Also try **dim** mode to compare.
- [ ] Confirm the **dashboard still works while the screen is dark** (that's the
      whole point of no external button).
- [ ] Over a day, glance at the screen and confirm the layout has **shifted a few
      pixels** (anti-burn-in jitter) — labels shouldn't sit on fixed pixels.

## 7. Recovery paths

- [ ] **Dashboard reconfigure**: Settings → Reconfigure WiFi → device reboots to
      the `AirBox-Setup` portal.
- [ ] **BOOT-button reset**: with the device running, hold **BOOT ~3 s** → it
      wipes WiFi and returns to the portal. (Press *after* boot, not during.)
- [ ] After either reset, re-provision and confirm the **IAQ calibration level is
      preserved** (it should not reset to 0 from a WiFi reset).
- [ ] **Bad-credentials fallback**: provision with a wrong password on purpose →
      after ~30 s it should fall back to the portal on its own (no brick).

## 8. Browser OTA

- [ ] Bump `FW_VERSION` in `config.h` (e.g. `1.0.1`), recompile to a `.bin`:
      `arduino-cli compile --fqbn $FQBN --output-dir build firmware/airbox`
      (the `.bin` lands in `build/`).
- [ ] Go to `http://airbox.local/update` (no login) and upload the `.bin`.
      Device reboots; confirm the new version on the Diagnostics tab. Confirm it
      did **not** reset/hang mid-flash.

## 9. Burn-in & calibration

- [ ] Leave it powered and on WiFi for **24–48 h**. Watch the IAQ **calibration**
      indicator climb unreliable → low → medium → high.
- [ ] Confirm it stays up over the period (uptime keeps climbing, no reboots),
      and that the calibration **survives a power-cycle** (BSEC state is saved to
      flash after accuracy ≥ 1).

## 10. Prep for gifting

- [ ] Factory-reset WiFi (BOOT hold) so it boots into the **portal** for your
      friend — but leave the BSEC calibration intact so he gets a head start.
- [ ] Print/share: the setup steps in [`first-time-setup.md`](first-time-setup.md)
      and the dashboard guide in [`dashboard.md`](dashboard.md).
- [ ] Push the repo public and send him the link.
- [ ] (Optional) Drop the enclosure STL/STEP into
      [`hardware/enclosure/`](../hardware/enclosure/) and commit.

---

### Notes / gotchas to remember

- If upload won't start: **double-tap RESET** for the bootloader.
- No serial output? Ensure "USB CDC On Boot" is enabled (it is in the default
  FQBN build used here).
- mDNS (`airbox.local`) can be flaky on some Android/Windows setups — the raw IP
  always works as a fallback.
- Pin the library versions you flashed with (ESP Async WebServer 3.11.0,
  Async TCP 3.4.10, ElegantOTA 3.1.7, esp32 core 3.3.8) so a future rebuild is
  reproducible.
