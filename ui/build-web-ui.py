#!/usr/bin/env python3
"""Bundle the dashboard UI source into the firmware's web_ui.h.

The editable source lives here in ui/:
    index.html      - markup + CSS, loads airbox-app.js via <script src=...>
    airbox-app.js   - dashboard logic (vanilla JS)

This inlines airbox-app.js into index.html and writes the result into the
INDEX_HTML PROGMEM raw-string in firmware/airbox/web_ui.h. PORTAL_HTML and the
rest of web_ui.h are left untouched.

Workflow:
    1. Edit ui/index.html / ui/airbox-app.js (open index.html in a browser to
       preview; it falls back to mock data when there's no device).
    2. python3 ui/build-web-ui.py
    3. Recompile / flash the firmware.

No external tools or packages required (standard library only).
"""
import re
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
WEBUI = ROOT / "firmware" / "airbox" / "web_ui.h"
SCRIPT_TAG = '<script src="airbox-app.js"></script>'

def main():
    html = (HERE / "index.html").read_text()
    js = (HERE / "airbox-app.js").read_text()

    if SCRIPT_TAG not in html:
        raise SystemExit(f"index.html must contain exactly: {SCRIPT_TAG}")
    bundled = html.replace(SCRIPT_TAG, "<script>\n" + js.rstrip("\n") + "\n</script>")

    # Raw-string delimiter safety: the body must not contain the closing token.
    if ')=====' + '"' in bundled:
        raise SystemExit('bundled HTML contains the )=====" delimiter; change the delimiter')

    webui = WEBUI.read_text()
    pat = re.compile(r'(const char INDEX_HTML\[\] PROGMEM = R"=====\().*?(\)=====";)',
                     re.DOTALL)
    if not pat.search(webui):
        raise SystemExit("INDEX_HTML PROGMEM block not found in web_ui.h")
    new = pat.sub(lambda m: m.group(1) + bundled + m.group(2), webui, count=1)

    WEBUI.write_text(new)
    print(f"web_ui.h INDEX_HTML updated: {len(bundled):,} bytes "
          f"({len(bundled)/1024:.1f} KB) from index.html + airbox-app.js")

if __name__ == "__main__":
    main()
