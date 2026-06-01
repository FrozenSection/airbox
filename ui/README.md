# AirBox dashboard UI (source)

The dashboard the firmware serves at `http://airbox.local` lives here as editable
source, then gets bundled into the firmware.

| File | Role |
|---|---|
| `index.html` | Markup + CSS. Loads the JS via `<script src="airbox-app.js">`. |
| `airbox-app.js` | Dashboard logic (vanilla JS, canvas charts). |
| `build-web-ui.py` | Inlines the JS into the HTML and writes it into `firmware/airbox/web_ui.h` (`INDEX_HTML`). |

**This is the single source of truth.** `firmware/airbox/web_ui.h`'s `INDEX_HTML`
block is a *generated* artifact — don't hand-edit it; edit the files here and
rebuild.

## Edit & preview
Open `index.html` directly in a browser. With no device on the network it falls
back to built-in **mock data**, so you can iterate on layout/logic offline. To
preview against a real device, serve this folder and point the fetches at the
device (or just flash and test on hardware).

## Build into the firmware
```sh
python3 ui/build-web-ui.py      # regenerates firmware/airbox/web_ui.h
```
Standard-library Python only — no packages needed. Then recompile/flash.

Notes:
- `PORTAL_HTML` (the captive-portal page) is hand-written in `web_ui.h` and is
  **not** touched by the bundler.
- The bundler refuses to run if the HTML contains the `)=====`​`"` raw-string
  delimiter (would break the C string).
- Keep it dependency-free: no CDNs, web fonts, or external libraries — the device
  serves this on a network that may have no internet.
