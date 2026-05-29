// config.h — build-time defaults and feature flags for AirBox.
//
// IMPORTANT: nothing secret lives here. WiFi credentials, the admin/OTA
// password, and (optional) MQTT credentials are all entered through the
// device's web UI at runtime and stored in NVS. This file only holds
// non-secret defaults and compile-time switches, so it is safe to commit
// to a public repository.

#ifndef CONFIG_H
#define CONFIG_H

// ----- Firmware identity -----
#define FW_VERSION        "1.0.0"
#define DEVICE_MODEL      "QT Py ESP32-S3 + BME688 + HDC3022 (standalone)"

// ----- Feature flags -----
// MQTT / Home Assistant discovery. OFF by default — this is a standalone
// device. Set to 1 only if you later want to also push to an MQTT broker;
// broker host/user/pass are then entered in the web Settings page.
#define ENABLE_MQTT          0

// ArduinoOTA (network flashing from the Arduino IDE / PlatformIO / espota).
// OFF by default — browser-based OTA (ElegantOTA at /update) is the primary
// update path for a standalone gift. Turn on if you flash from a dev setup.
#define ENABLE_ARDUINO_OTA   0

// ----- Captive-portal / AP defaults -----
// Setup access point is intentionally OPEN (no password) so the OLED join-QR
// stays small enough to render crisply on the 0.96" screen, and so first-time
// setup is one scan-and-tap. The AP only ever serves the provisioning page.
#define AP_SSID           "AirBox-Setup"
#define AP_IP_STR         "192.168.4.1"

// ----- Runtime config defaults (overridable in the web Settings page) -----
#define DEFAULT_DEVICE_NAME  "AirBox"
#define DEFAULT_HOSTNAME     "airbox"        // -> http://airbox.local
#define DEFAULT_TEMP_UNIT    'F'             // 'F' or 'C'
#define DEFAULT_ADMIN_PASS   "airbox"        // protects /update and Settings; CHANGE in UI

// ----- OLED longevity (all overridable in the web Settings page) -----
// SSD1306 contrast 0-255. Lower current = longer OLED life; the UI exposes
// Low/Medium/High/Max. A moderate default is gentler than the panel's ~0xCF.
#define DEFAULT_BRIGHTNESS   64
// Night mode: between these local hours, either fully blank the screen (saves
// the most life, keeps a bedroom dark) or just dim it. Off by default; needs
// the UTC offset for the clock.
#define DEFAULT_NIGHT_ENABLE 0
#define DEFAULT_NIGHT_START  23              // hour 0-23, local
#define DEFAULT_NIGHT_END    7
#define DEFAULT_NIGHT_MODE   0               // 0 = blank (screen off), 1 = dim
#define NIGHT_DIM_CONTRAST   4               // SSD1306 contrast used in dim mode (very low)
#define DEFAULT_UTC_OFFSET_H 0               // local = UTC + this many hours (NTP)
#define NTP_SERVER           "pool.ntp.org"
// Anti-burn-in: nudge the whole layout a few px on this cycle so static labels
// never sit on the same pixels forever.
#define PIXEL_SHIFT_INTERVAL_MS  300000UL    // 5 minutes

// ----- Trend history ring buffer -----
// 288 samples @ 5 min spacing = 24 h of trend. 4 channels x 288 x 4 B ≈ 4.6 KB.
#define HISTORY_SAMPLES        288
#define HISTORY_INTERVAL_MS    300000UL      // 5 minutes

// ----- Pins / hardware -----
#define I2C_SDA_PIN       41                 // STEMMA QT (Wire1)
#define I2C_SCL_PIN       40
#define BOOT_BUTTON_PIN   0                  // QT Py BOOT button (read post-boot only)

// ----- Watchdog -----
#define WATCHDOG_TIMEOUT_MS    60000UL       // 60 s; extended to 5 min during OTA flash
#define OTA_WATCHDOG_MS        300000UL      // 5 min

// ----- Factory-reset hold -----
#define FACTORY_RESET_HOLD_MS  3000UL        // hold BOOT this long (in run/portal) to wipe WiFi

#endif // CONFIG_H
