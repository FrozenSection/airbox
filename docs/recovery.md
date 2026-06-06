# Changing networks & recovery

This device is designed so you can never paint yourself into a corner — there
are three independent ways back to a working state, from fully software to
fully physical.

## 1. It can't join WiFi (automatic)

If the saved network is gone, renamed, or has a new password, the device tries
to connect for ~30 seconds at boot and then **automatically falls back to the
`AirBox-Setup` portal**. Just redo [first-time setup](first-time-setup.md).
You don't have to do anything to trigger this.

## 2. Reconfigure from the dashboard

If want to move it to a different network:

1. Open `http://airbox.local` → **Settings** tab.
2. Tap **Reconfigure WiFi** and confirm.
3. The device reboots into the `AirBox-Setup` portal — rejoin it and pick the
   new network.

## 3. Physical reset — hold BOOT (requires opening the enclosure)

A last resort if the dashboard is unreachable *and* the device won't fall back
to the portal on its own. The BOOT button is **inside the enclosure** (no
external button or pin-hole), so this one needs you to **open the case first** —
no trouble with a couple of screws/clips, just not a no-tools step. Once it's
open:

1. With the device powered and running, **press and hold the BOOT button for
   about 3 seconds.**
2. It wipes the saved WiFi credentials and reboots into the `AirBox-Setup`
   portal.

> Press BOOT **after** the device has booted — don't hold it during power-up
> (holding it at reset puts the ESP32-S3 into USB flashing mode instead, which
> is a different thing).
>
> In practice you'll rarely get here: methods 1 and 2 above need no tools, and
> cover almost every "can't reach it" case. Reach for BOOT only when both fail.

## What is *not* erased

A WiFi reset (any of the above) clears **only the network credentials**. These
are preserved:

- The **air-quality (BSEC) calibration** — so you don't lose the 24–48 h of
  learning every time you change networks.
- Your **device name**, **temperature unit**, and **hostname** settings.

## Full wipe (start completely fresh)

To also clear the air-quality calibration, use **Settings → Recalibrate air
sensor** on the dashboard. IAQ accuracy drops to 0 and re-learns over 24–48 h.

## Updating firmware

- **Browser (normal):** Settings → **Firmware update**, or go to
  `http://airbox.local/update`. The updater asks for a login (the firmware
  update is the one password-protected action on the device); enter the
  credentials provided with the unit, then upload the new `.bin`. The device
  reboots automatically.
- **USB:** re-flash from the Arduino IDE over USB-C any time (no login needed).

If an update is ever interrupted (power loss mid-flash), the device's bootloader
keeps the previous working firmware on its other partition; power-cycle and
retry, or re-flash over USB. A normal USB re-flash needs no button presses; only
a fully unresponsive board would need manual DFU mode (hold the internal BOOT
button at power-up), which means opening the enclosure.
