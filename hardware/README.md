# Hardware — enclosure

3D-printable enclosure and mounting files for the standalone AirBox.

> **Files coming soon.** Drop the enclosure models in [`enclosure/`](enclosure/)
> — STL for printing, plus a STEP or native CAD source for remixing.

## Suggested contents (as files are added)

```
enclosure/
├── airbox-case.stl        # main body
├── airbox-lid.stl         # lid / vented front
├── airbox.step            # editable CAD source
└── ...
```

## Design notes / requirements

The enclosure needs to accommodate the airflow the air-quality sensor depends
on, and keep the BME688 away from the QT Py's self-heating:

- **Vents** near the BME688 so ambient air reaches the gas/humidity sensor.
  Without airflow, humidity and IAQ readings lag and drift.
- **Separate the BME688 from the ESP32-S3** a little — the MCU and the BME688
  heater both produce heat. The HDC3022 is the trusted temperature source, so
  give *it* good ambient exposure too.
- **OLED cutout** (optional build) for the 0.96" 128×64 display.
- **USB-C access** for power and recovery flashing.
- **BOOT button access** (or a pin hole) for the WiFi-reset hold.
- Daisy-chained STEMMA QT cables need a little internal slack.

## Print settings (fill in once finalized)

| Setting | Value |
|---|---|
| Material | PLA / PETG |
| Layer height | 0.2 mm |
| Infill | 15–20 % |
| Supports | _TBD per part_ |

## License

Enclosure files are licensed **CC BY 4.0** — see [LICENSE](LICENSE). (The
firmware is MIT; see the repository root.)
