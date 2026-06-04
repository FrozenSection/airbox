# AirBox dashboard UI (source)

The dashboard the firmware serves at `http://airbox.local` lives here as editable
source, then gets bundled into the firmware.

| File | Role |
|---|---|
| `index.html` | Markup + CSS. Loads the JS via `<script src="airbox-app.js">`. |
| `airbox-app.js` | Dashboard logic (vanilla JS, canvas charts). |
| `build-web-ui.py` | Inlines the JS, **gzip-compresses** the page, and writes the `INDEX_HTML_GZ` byte array into `firmware/airbox/web_ui.h`. |

**This is the single source of truth.** `firmware/airbox/web_ui.h`'s `INDEX_HTML_GZ`
block (a gzip byte array, between the `>>> GENERATED >>>` markers) is a *generated*
artifact — don't hand-edit it; edit the files here and rebuild.

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
- The page is gzip-compressed into `INDEX_HTML_GZ` and served **zero-copy** with
  `Content-Encoding: gzip` (the `/` handler streams it straight from flash) — small
  on the wire and no per-request heap copy. The mock-data fallback only kicks in
  for `file://` preview; on the device a failed fetch shows a stale banner, never
  fake data.
- Keep it dependency-free: no CDNs, web fonts, or external libraries — the device
  serves this on a network that may have no internet.
