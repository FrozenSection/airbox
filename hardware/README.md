# Hardware — enclosure

3D-printable enclosure and mounting files for the standalone AirBox.

## Files

| File | Print? | Purpose |
|---|---|---|
| `enclosure/AirBox_Enclosure.STL` | ✅ ×1 | Main body — the three-chamber enclosure |
| `enclosure/AirBox_Back.STL` | ✅ ×1 | Back cover |
| `enclosure/AirBox.3MF` | — | Both parts arranged in one project file (load this if your slicer prefers it) |
| `enclosure/AirBox.STEP` | — | Editable CAD source, for remixing |

Print `AirBox_Enclosure.STL` **and** `AirBox_Back.STL` (one each). The `.3MF` is
those same two parts bundled together for convenience; the `.STEP` is the
editable source for anyone who wants to modify the design.

## Design notes / requirements

The enclosure needs to accommodate the airflow the air-quality sensor depends
on, and keep the HDC3022 and BME688 away from heat sources:

- **Three separate chambers** to isolate the HDC3022, BME688, and QT Py ESP32/OLED display
- **Vents** in all three chambers for adequate air flow and heat dissipation.
- **OLED cutout** for the 0.96" 128×64 display.
- **USB-C access** for power and recovery flashing.
- **No external BOOT button or pin-hole** — the QT Py's BOOT button (used for the
  physical WiFi reset and for DFU recovery) stays sealed inside, so reaching it
  means **opening the enclosure**. This is fine in normal use: WiFi reset and
  reconfiguration are available from the dashboard (Settings → Reconfigure WiFi),
  and firmware updates happen over the network or USB-C without opening the case.
  See [recovery](../docs/recovery.md).
- Daisy-chained STEMMA QT cables need a little internal slack.

## Print settings

| Setting | Value |
|---|---|
| Material | PETG |
| Layer height | 0.2 mm |
| Infill | Gyroid: 15% |
| Supports | _TBD per part and as needed_ |

## License

Enclosure files are licensed **CC BY 4.0** — see [LICENSE](LICENSE). (The
firmware is MIT; see the repository root.)
