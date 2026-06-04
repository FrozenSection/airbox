// AirBox — QT Py ESP32-S3 + BME688 (BSEC2) + HDC3022 + optional SSD1306
//
// A STANDALONE indoor environmental monitor. No Home Assistant required:
//   * WiFi is provisioned through a captive portal (OLED shows a join-QR).
//   * Data is served from a local web dashboard (live values + trend charts).
//   * Firmware updates happen in the browser (ElegantOTA at /update).
//   * mDNS publishes the dashboard at http://<hostname>.local
//
// Forked from the Home Assistant MQTT version. The hardened sensor core
// (BSEC2 + CRC32 NVS calibration, HDC3022 read path, per-sensor health /
// staleness / reinit, 60 s task watchdog) is preserved as-is. MQTT/HA
// discovery is retained behind ENABLE_MQTT (config.h), OFF by default.
//
// Recovery paths (see docs/recovery.md):
//   1. Auto-fallback to the setup portal if WiFi won't connect at boot.
//   2. Hold the BOOT button ~3 s (any time) to wipe WiFi and re-provision.
//   3. "Reconfigure WiFi" button on the dashboard.
// Calibration (NVS "bsec") is preserved across a WiFi reset.

#include "config.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <bsec2.h>
#include <Adafruit_HDC302x.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_task_wdt.h>
#include <esp_system.h>  // esp_reset_reason()
#include <esp_heap_caps.h>  // heap_caps_get_largest_free_block() — fragmentation diag
#include <time.h>
#include <LittleFS.h>    // persistent trend history
#include "qrcode.h"  // QR encoder bundled in the ESP32 core (espressif__qrcode)

#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

#include "web_ui.h"

#if ENABLE_MQTT
#include <PubSubClient.h>
#endif
#if ENABLE_ARDUINO_OTA
#include <ArduinoOTA.h>
#endif

// =================== OLED (optional) ===================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);
bool displayOK = false;

// =================== Sensors ===================
Bsec2 envSensor;
Adafruit_HDC302x hdc;
bool bmeOK = false;
bool hdcOK = false;

const bsecSensor bsecSubscriptionList[] = {
  BSEC_OUTPUT_STATIC_IAQ,
  BSEC_OUTPUT_CO2_EQUIVALENT,
  BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
  BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
  BSEC_OUTPUT_RAW_TEMPERATURE,
  BSEC_OUTPUT_RAW_HUMIDITY,
  BSEC_OUTPUT_RAW_PRESSURE,
};
const uint8_t bsecSubscriptionCount =
  sizeof(bsecSubscriptionList) / sizeof(bsecSubscriptionList[0]);

Preferences bsecPrefs;
uint8_t bsecStateBlob[BSEC_MAX_STATE_BLOB_SIZE];
bool bsecStateSavedOnce = false;

struct SensorData {
  float hdcTempC, hdcRH;
  float bmeTempC, bmeRH, bmePresHpa;
  float bmeIaq, bmeEco2, bmeBvoc, bmeCompTempC;
  uint8_t bmeIaqAccuracy;
  bool hdcValid;
  bool bmeValid;
  unsigned long hdcLastGoodMs;
  unsigned long bmeLastGoodMs;
  uint16_t hdcFailStreak;
  uint16_t bmeFailStreak;
  uint32_t hdcTotalFails;
  uint32_t bmeTotalFails;
};
SensorData latest = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, false, 0, 0, 0, 0, 0, 0};

// Cadences
const unsigned long READ_INTERVAL_DISPLAY_MS = 3000;
const unsigned long READ_INTERVAL_HEADLESS_MS = 10000;
const unsigned long SENSOR_REINIT_INTERVAL_MS = 300000;
const unsigned long BSEC_STATE_SAVE_INTERVAL_MS = 3UL * 3600UL * 1000UL;
const unsigned long WIFI_FORCE_RECONNECT_MS = 300000;
const unsigned long MAX_VALUE_AGE_MS = 300000;
const uint16_t MAX_FAIL_STREAK = 20;

unsigned long lastRead = 0;
unsigned long lastBmeReinit = 0;
unsigned long lastHdcReinit = 0;
unsigned long lastWifiReconnect = 0;
unsigned long lastBsecStateSave = 0;
unsigned long lastHistMs = 0;

// =================== Trend history ring buffer ===================
// Static allocation (link-time accounted, no heap fragmentation). Stores raw
// values; temperature is converted to the display unit when served.
float    histT[HISTORY_SAMPLES];    // °C (raw)
float    histRH[HISTORY_SAMPLES];   // %
float    histP[HISTORY_SAMPLES];    // hPa
float    histIAQ[HISTORY_SAMPLES];  // IAQ
uint32_t histTime[HISTORY_SAMPLES]; // UTC epoch seconds (0 = before clock sync)
int histHead = 0;
int histCount = 0;
bool fsOK = false;                  // LittleFS mounted
unsigned long lastHistSave = 0;

// =================== Timezones ===================
// Named zones -> POSIX TZ rule (DST handled automatically). cfg.tzIndex stores
// the position. KEEP THIS IN SYNC with the <select id="sTz"> options in web_ui.h.
struct TzZone { const char* label; const char* posix; };
const TzZone TZ_TABLE[] = {
  {"UTC",                    "UTC0"},
  {"Eastern (New York)",     "EST5EDT,M3.2.0,M11.1.0"},
  {"Central (Chicago)",      "CST6CDT,M3.2.0,M11.1.0"},
  {"Mountain (Denver)",      "MST7MDT,M3.2.0,M11.1.0"},
  {"Arizona (no DST)",       "MST7"},
  {"Pacific (Los Angeles)",  "PST8PDT,M3.2.0,M11.1.0"},
  {"Alaska (Anchorage)",     "AKST9AKDT,M3.2.0,M11.1.0"},
  {"Hawaii (no DST)",        "HST10"},
  {"UK (London)",            "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Central Europe",         "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"India (Kolkata)",        "IST-5:30"},
  {"Japan (Tokyo)",          "JST-9"},
  {"Sydney",                 "AEST-10AEDT,M10.1.0,M4.1.0/3"},
};
const uint8_t TZ_COUNT = sizeof(TZ_TABLE) / sizeof(TZ_TABLE[0]);

// =================== Runtime config (NVS "cfg") ===================
struct Config {
  String ssid;
  String pass;
  String devName;
  String hostname;
  String adminPass;
  char   unit;  // 'C' or 'F'
  uint8_t brightness;   // SSD1306 contrast 0-255
  bool    nightEn;      // blank/dim the OLED on a schedule
  uint16_t nightStartMin;  // minutes-of-day 0-1439 (local)
  uint16_t nightEndMin;
  uint8_t nightMode;    // 0 = blank (off), 1 = dim
  uint8_t tzIndex;      // index into TZ_TABLE (DST-aware local time)
  float   comfortTminC, comfortTmaxC;   // comfort band, °C canonical
  uint8_t comfortHmin,  comfortHmax;    // comfort band, %
#if ENABLE_MQTT
  String mqttHost;
  String mqttUser;
  String mqttPass;
#endif
};
Config cfg;
Preferences cfgPrefs;

// =================== Web / network ===================
AsyncWebServer server(80);
DNSServer dnsServer;
String deviceId;     // qtpy_xxxxxx (from MAC)
IPAddress apIP(192, 168, 4, 1);

enum Mode { MODE_PORTAL, MODE_RUN };
Mode mode = MODE_RUN;

// Cross-task action flags. Web handlers run on the AsyncTCP task and MUST NOT
// block (no I2C / NVS / restart). They only set these flags; loop() performs
// the work on the main task.
volatile bool pendingSaveWifi = false;
volatile bool pendingSaveSettings = false;
volatile bool pendingRestart = false;
volatile bool pendingReconfigure = false;
volatile bool pendingRecalibrate = false;
volatile bool pendingFactoryReset = false;

// Staging buffers for the above (written in handlers, read in loop()).
String stgSsid, stgWifiPass;
String stgName, stgUnit, stgHost, stgPass;
String stgBright, stgNight, stgNStart, stgNEnd, stgNMode, stgTz;
String stgCtmin, stgCtmax, stgChmin, stgChmax, stgCunit;
#if ENABLE_MQTT
String stgMqttHost, stgMqttUser, stgMqttPass;
#endif

// =================== MQTT (optional) ===================
#if ENABLE_MQTT
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
String haDeviceName, roomSlug, stateTopic, availTopic;
const char* DISCOVERY_PREFIX = "homeassistant";
const uint16_t MQTT_PORT = 1883;
unsigned long lastPublish = 0;
unsigned long lastMqttAttempt = 0;
const unsigned long PUBLISH_INTERVAL_MS = 30000;
const unsigned long MQTT_RETRY_MS = 5000;
#endif

static inline bool inRange(float v, float lo, float hi) {
  return !isnan(v) && !isinf(v) && v >= lo && v <= hi;
}
static inline float toUnit(float c) {
  return (cfg.unit == 'F') ? (c * 9.0f / 5.0f + 32.0f) : c;
}

// =================== Watchdog helpers ===================
void wdtReconfigure(uint32_t ms) {
  esp_task_wdt_config_t c = {.timeout_ms = ms, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&c);
}

// =================== Config store ===================
void loadConfig() {
  cfgPrefs.begin("cfg", true);  // read-only
  cfg.ssid      = cfgPrefs.getString("ssid", "");
  cfg.pass      = cfgPrefs.getString("pass", "");
  cfg.devName   = cfgPrefs.getString("name", DEFAULT_DEVICE_NAME);
  cfg.hostname  = cfgPrefs.getString("host", DEFAULT_HOSTNAME);
  cfg.adminPass = cfgPrefs.getString("admin", DEFAULT_ADMIN_PASS);
  cfg.unit      = cfgPrefs.getString("unit", String(DEFAULT_TEMP_UNIT))[0];
  cfg.brightness = cfgPrefs.getUChar("bright", DEFAULT_BRIGHTNESS);
  cfg.nightEn    = cfgPrefs.getBool("nightEn", DEFAULT_NIGHT_ENABLE);
  // Night window is stored as minutes-of-day. Migrate from the legacy hour keys
  // (nStart/nEnd) if the minute keys aren't present yet.
  cfg.nightStartMin = cfgPrefs.getUShort("nsm", (uint16_t)cfgPrefs.getUChar("nStart", DEFAULT_NIGHT_START) * 60);
  cfg.nightEndMin   = cfgPrefs.getUShort("nem", (uint16_t)cfgPrefs.getUChar("nEnd", DEFAULT_NIGHT_END) * 60);
  cfg.nightMode  = cfgPrefs.getUChar("nMode", DEFAULT_NIGHT_MODE);
  cfg.tzIndex = cfgPrefs.getUChar("tz", DEFAULT_TZ_INDEX);
  if (cfg.tzIndex >= TZ_COUNT) cfg.tzIndex = DEFAULT_TZ_INDEX;
  cfg.comfortTminC = cfgPrefs.getFloat("ctmin", DEFAULT_COMFORT_TMIN_C);
  cfg.comfortTmaxC = cfgPrefs.getFloat("ctmax", DEFAULT_COMFORT_TMAX_C);
  cfg.comfortHmin  = cfgPrefs.getUChar("chmin", DEFAULT_COMFORT_HMIN);
  cfg.comfortHmax  = cfgPrefs.getUChar("chmax", DEFAULT_COMFORT_HMAX);
#if ENABLE_MQTT
  cfg.mqttHost  = cfgPrefs.getString("mhost", "");
  cfg.mqttUser  = cfgPrefs.getString("muser", "");
  cfg.mqttPass  = cfgPrefs.getString("mpass", "");
#endif
  cfgPrefs.end();
  if (cfg.unit != 'C' && cfg.unit != 'F') cfg.unit = DEFAULT_TEMP_UNIT;
}

void saveWifiCreds(const String& ssid, const String& pass) {
  cfgPrefs.begin("cfg", false);
  cfgPrefs.putString("ssid", ssid);
  cfgPrefs.putString("pass", pass);
  cfgPrefs.end();
}

// Clears ONLY the WiFi credentials — device name, unit, and the "bsec"
// calibration namespace are preserved across a WiFi reset.
void clearWifiCreds() {
  cfgPrefs.begin("cfg", false);
  cfgPrefs.remove("ssid");
  cfgPrefs.remove("pass");
  cfgPrefs.end();
}

void setContrast(uint8_t v);     // defined with the OLED rendering helpers

// Apply the selected timezone so localtime_r() returns DST-correct local time,
// and (re)start the NTP sync.
void applyTimezone() {
  uint8_t i = (cfg.tzIndex < TZ_COUNT) ? cfg.tzIndex : 0;
  configTzTime(TZ_TABLE[i].posix, NTP_SERVER);
}

void applyAndSaveSettings() {
  if (stgName.length()) cfg.devName = stgName;
  if (stgUnit.length()) cfg.unit = stgUnit[0];
  if (stgHost.length()) cfg.hostname = stgHost;
  if (stgPass.length()) cfg.adminPass = stgPass;  // blank = keep current
  if (cfg.unit != 'C' && cfg.unit != 'F') cfg.unit = DEFAULT_TEMP_UNIT;
  if (stgBright.length()) cfg.brightness = (uint8_t)stgBright.toInt();
  if (stgNight.length())  cfg.nightEn = (stgNight.toInt() != 0);
  if (stgNStart.length()) cfg.nightStartMin = (uint16_t)constrain(stgNStart.toInt(), 0, 1439);
  if (stgNEnd.length())   cfg.nightEndMin = (uint16_t)constrain(stgNEnd.toInt(), 0, 1439);
  if (stgNMode.length())  cfg.nightMode = (stgNMode.toInt() != 0) ? 1 : 0;
  if (stgTz.length()) {
    uint8_t t = (uint8_t)stgTz.toInt();
    cfg.tzIndex = (t < TZ_COUNT) ? t : 0;
  }
  // Comfort targets — temp bands arrive in comfort_unit; store canonical °C.
  if (stgCtmin.length()) cfg.comfortTminC = (stgCunit == "F") ? (stgCtmin.toFloat() - 32.0f) * 5.0f / 9.0f : stgCtmin.toFloat();
  if (stgCtmax.length()) cfg.comfortTmaxC = (stgCunit == "F") ? (stgCtmax.toFloat() - 32.0f) * 5.0f / 9.0f : stgCtmax.toFloat();
  if (stgChmin.length()) cfg.comfortHmin = (uint8_t)constrain(stgChmin.toInt(), 0, 100);
  if (stgChmax.length()) cfg.comfortHmax = (uint8_t)constrain(stgChmax.toInt(), 0, 100);
  cfgPrefs.begin("cfg", false);
  cfgPrefs.putString("name", cfg.devName);
  cfgPrefs.putString("unit", String(cfg.unit));
  cfgPrefs.putString("host", cfg.hostname);
  cfgPrefs.putString("admin", cfg.adminPass);
  cfgPrefs.putUChar("bright", cfg.brightness);
  cfgPrefs.putBool("nightEn", cfg.nightEn);
  cfgPrefs.putUShort("nsm", cfg.nightStartMin);
  cfgPrefs.putUShort("nem", cfg.nightEndMin);
  cfgPrefs.putUChar("nMode", cfg.nightMode);
  cfgPrefs.putUChar("tz", cfg.tzIndex);
  cfgPrefs.putFloat("ctmin", cfg.comfortTminC);
  cfgPrefs.putFloat("ctmax", cfg.comfortTmaxC);
  cfgPrefs.putUChar("chmin", cfg.comfortHmin);
  cfgPrefs.putUChar("chmax", cfg.comfortHmax);
#if ENABLE_MQTT
  if (stgMqttHost.length()) cfg.mqttHost = stgMqttHost;
  if (stgMqttUser.length()) cfg.mqttUser = stgMqttUser;
  if (stgMqttPass.length()) cfg.mqttPass = stgMqttPass;
  cfgPrefs.putString("mhost", cfg.mqttHost);
  cfgPrefs.putString("muser", cfg.mqttUser);
  cfgPrefs.putString("mpass", cfg.mqttPass);
#endif
  cfgPrefs.end();
  // Apply the display + clock changes live (we're in loop() context).
  if (displayOK) setContrast(cfg.brightness);
  applyTimezone();
  Serial.println("Settings saved");
}

// =================== BSEC callback + NVS state (preserved) ===================
void bsecDataCallback(bme68x_data /*data*/, bsecOutputs outputs, Bsec2 /*bsec*/) {
  if (!outputs.nOutputs) return;
  for (uint8_t i = 0; i < outputs.nOutputs; i++) {
    const bsecData out = outputs.output[i];
    switch (out.sensor_id) {
      case BSEC_OUTPUT_STATIC_IAQ:
        latest.bmeIaq = out.signal;
        latest.bmeIaqAccuracy = out.accuracy;
        break;
      case BSEC_OUTPUT_CO2_EQUIVALENT:
        latest.bmeEco2 = out.signal;
        break;
      case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
        latest.bmeBvoc = out.signal;
        break;
      case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
        if (inRange(out.signal, -40.0f, 85.0f)) latest.bmeCompTempC = out.signal;
        break;
      case BSEC_OUTPUT_RAW_TEMPERATURE:
        if (inRange(out.signal, -40.0f, 85.0f)) latest.bmeTempC = out.signal;
        break;
      case BSEC_OUTPUT_RAW_HUMIDITY:
        if (inRange(out.signal, 0.0f, 100.0f)) latest.bmeRH = out.signal;
        break;
      case BSEC_OUTPUT_RAW_PRESSURE:
        if (inRange(out.signal, 800.0f, 1100.0f)) latest.bmePresHpa = out.signal;
        break;
      default:
        break;
    }
  }
  latest.bmeValid = true;
  latest.bmeLastGoodMs = millis();
  latest.bmeFailStreak = 0;
}

void logBsecStatus(const char* context) {
  Serial.printf("  %s — BSEC status=%d, BME68x status=%d\n",
                context, (int)envSensor.status, (int)envSensor.sensor.status);
}

static uint32_t bsecCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  return ~crc;
}

void loadBsecState() {
  const size_t blobWithCrcSize = 4 + BSEC_MAX_STATE_BLOB_SIZE;
  uint8_t combined[blobWithCrcSize];
  bsecPrefs.begin("bsec", true);
  size_t got = bsecPrefs.getBytes("state", combined, blobWithCrcSize);
  bsecPrefs.end();

  if (got == BSEC_MAX_STATE_BLOB_SIZE) {
    memcpy(bsecStateBlob, combined, BSEC_MAX_STATE_BLOB_SIZE);
    if (envSensor.setState(bsecStateBlob))
      Serial.println("BSEC state restored from NVS (legacy format, no CRC)");
    else
      Serial.println("BSEC setState() failed on legacy blob — starting fresh");
    return;
  }
  if (got != blobWithCrcSize) {
    Serial.printf("No saved BSEC state (got %u bytes) — fresh calibration\n", (unsigned)got);
    return;
  }
  uint32_t storedCrc;
  memcpy(&storedCrc, combined, 4);
  uint32_t computedCrc = bsecCrc32(combined + 4, BSEC_MAX_STATE_BLOB_SIZE);
  if (storedCrc != computedCrc) {
    Serial.printf("BSEC state CRC mismatch (stored=0x%08X, computed=0x%08X) — starting fresh\n",
                  (unsigned)storedCrc, (unsigned)computedCrc);
    return;
  }
  memcpy(bsecStateBlob, combined + 4, BSEC_MAX_STATE_BLOB_SIZE);
  if (envSensor.setState(bsecStateBlob))
    Serial.println("BSEC state restored from NVS (CRC verified)");
  else
    Serial.println("BSEC setState() failed despite valid CRC — starting fresh");
}

void saveBsecState() {
  if (!envSensor.getState(bsecStateBlob)) {
    Serial.println("BSEC getState() failed, skipping NVS save");
    return;
  }
  const size_t blobWithCrcSize = 4 + BSEC_MAX_STATE_BLOB_SIZE;
  uint8_t combined[blobWithCrcSize];
  uint32_t crc = bsecCrc32(bsecStateBlob, BSEC_MAX_STATE_BLOB_SIZE);
  memcpy(combined, &crc, 4);
  memcpy(combined + 4, bsecStateBlob, BSEC_MAX_STATE_BLOB_SIZE);
  bsecPrefs.begin("bsec", false);
  bsecPrefs.putBytes("state", combined, blobWithCrcSize);
  bsecPrefs.end();
  Serial.printf("BSEC state saved to NVS (accuracy=%u, crc=0x%08X)\n",
                (unsigned)latest.bmeIaqAccuracy, (unsigned)crc);
}

// =================== Sensor reads / reinit (preserved) ===================
void readSensors() {
  if (hdcOK) {
    double t = 0, h = 0;
    if (hdc.readTemperatureHumidityOnDemand(t, h, TRIGGERMODE_LP0)
        && inRange((float)t, -40.0f, 85.0f) && inRange((float)h, 0.0f, 100.0f)) {
      latest.hdcTempC = (float)t;
      latest.hdcRH = (float)h;
      latest.hdcValid = true;
      latest.hdcLastGoodMs = millis();
      latest.hdcFailStreak = 0;
    } else {
      latest.hdcFailStreak++;
      latest.hdcTotalFails++;
      Serial.printf("HDC read failed (streak=%u, total=%lu)\n",
                    latest.hdcFailStreak, (unsigned long)latest.hdcTotalFails);
    }
  }
  Serial.printf("HDC: %.2f C, %.1f%%RH | BME: %.2f C, %.1f%%RH, %.2f hPa | IAQ:%.0f acc:%u\n",
                latest.hdcTempC, latest.hdcRH, latest.bmeTempC, latest.bmeRH,
                latest.bmePresHpa, latest.bmeIaq, latest.bmeIaqAccuracy);
}

void reinitBme() {
  if (bmeOK) return;
  Serial.println("BME/BSEC reinit attempt...");
  if (envSensor.begin(BME68X_I2C_ADDR_HIGH, Wire1)) {
    envSensor.attachCallback(bsecDataCallback);
    loadBsecState();
    envSensor.updateSubscription(const_cast<bsecSensor*>(bsecSubscriptionList),
                                 bsecSubscriptionCount, BSEC_SAMPLE_RATE_LP);
    if ((int)envSensor.status >= 0) {
      bmeOK = true;
      latest.bmeFailStreak = 0;
      Serial.println("BME688/BSEC responsive again, awaiting first output");
    } else {
      Serial.println("BSEC subscription error during reinit — will retry in 5 min");
      logBsecStatus("reinit updateSubscription");
    }
  } else {
    Serial.println("BME reinit failed (begin() returned false) — will retry in 5 min");
    logBsecStatus("reinit begin");
  }
}

void reinitHdc() {
  if (hdcOK) return;
  Serial.println("HDC reinit attempt...");
  if (hdc.begin(0x44, &Wire1)) {
    hdcOK = true;
    latest.hdcFailStreak = 0;
    Serial.println("HDC3022 chip responsive again, awaiting first good read");
  } else {
    Serial.println("HDC reinit failed (begin() returned false) — will retry in 5 min");
  }
}

void recordHistory() {
  time_t now = time(nullptr);
  histTime[histHead] = (now > 1700000000) ? (uint32_t)now : 0;  // 0 until NTP syncs
  histT[histHead]   = (hdcOK && latest.hdcValid) ? latest.hdcTempC : NAN;
  histRH[histHead]  = (hdcOK && latest.hdcValid) ? latest.hdcRH : NAN;
  histP[histHead]   = (bmeOK && latest.bmeValid) ? latest.bmePresHpa : NAN;
  histIAQ[histHead] = (bmeOK && latest.bmeValid) ? latest.bmeIaq : NAN;
  histHead = (histHead + 1) % HISTORY_SAMPLES;
  if (histCount < HISTORY_SAMPLES) histCount++;
}

// Persist the ring buffer to LittleFS so the charts survive a reboot. Written
// on a slow cadence (+ before any planned restart), so worst-case power-loss
// data loss is one save interval — fine for trend data, easy on flash.
#define HIST_MAGIC 0x584F4241UL  // "ABOX"
void saveHistory() {
  if (!fsOK) return;
  File f = LittleFS.open(HISTORY_FILE, "w");
  if (!f) { Serial.println("history save: open failed"); return; }
  uint32_t magic = HIST_MAGIC;
  uint16_t ver = 1, n = HISTORY_SAMPLES;
  f.write((uint8_t*)&magic, 4);
  f.write((uint8_t*)&ver, 2);
  f.write((uint8_t*)&n, 2);
  f.write((uint8_t*)&histHead, sizeof(histHead));
  f.write((uint8_t*)&histCount, sizeof(histCount));
  f.write((uint8_t*)histT, sizeof(histT));
  f.write((uint8_t*)histRH, sizeof(histRH));
  f.write((uint8_t*)histP, sizeof(histP));
  f.write((uint8_t*)histIAQ, sizeof(histIAQ));
  f.write((uint8_t*)histTime, sizeof(histTime));
  f.close();
  Serial.printf("history saved (%d samples)\n", histCount);
}

void loadHistory() {
  if (!fsOK || !LittleFS.exists(HISTORY_FILE)) return;
  File f = LittleFS.open(HISTORY_FILE, "r");
  if (!f) return;
  uint32_t magic = 0;
  uint16_t ver = 0, n = 0;
  f.read((uint8_t*)&magic, 4);
  f.read((uint8_t*)&ver, 2);
  f.read((uint8_t*)&n, 2);
  if (magic != HIST_MAGIC || ver != 1 || n != HISTORY_SAMPLES) {
    Serial.println("history file mismatch — ignoring");
    f.close();
    return;
  }
  f.read((uint8_t*)&histHead, sizeof(histHead));
  f.read((uint8_t*)&histCount, sizeof(histCount));
  f.read((uint8_t*)histT, sizeof(histT));
  f.read((uint8_t*)histRH, sizeof(histRH));
  f.read((uint8_t*)histP, sizeof(histP));
  f.read((uint8_t*)histIAQ, sizeof(histIAQ));
  f.read((uint8_t*)histTime, sizeof(histTime));
  f.close();
  if (histHead < 0 || histHead >= HISTORY_SAMPLES) histHead = 0;
  if (histCount < 0 || histCount > HISTORY_SAMPLES) histCount = 0;
  Serial.printf("history restored (%d samples)\n", histCount);
}

// =================== OLED rendering ===================
// ----- OLED longevity: contrast, pixel-shift jitter, night blanking -----
bool displayPowered = true;
int  activeContrast = -1;     // currently-applied contrast (-1 = unset)
int  shiftX = 0, shiftY = 0;  // applied to every run-screen coordinate
unsigned long lastShift = 0;
const int8_t SHIFT_SEQ[][2] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1}};
uint8_t shiftIdx = 0;

void setContrast(uint8_t v) {
  if ((int)v == activeContrast) return;
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(v);
  activeContrast = v;
}
void updateShift() {
  shiftIdx = (shiftIdx + 1) % (sizeof(SHIFT_SEQ) / sizeof(SHIFT_SEQ[0]));
  shiftX = SHIFT_SEQ[shiftIdx][0];
  shiftY = SHIFT_SEQ[shiftIdx][1];
}
void setDisplayPower(bool on) {
  if (!displayOK || on == displayPowered) return;
  display.ssd1306_command(on ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
  displayPowered = on;
}
static bool timeSynced() { return time(nullptr) > 1700000000; }  // ~Nov 2023+
bool isNightNow() {
  if (!cfg.nightEn || cfg.nightStartMin == cfg.nightEndMin || !timeSynced()) return false;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int m = t.tm_hour * 60 + t.tm_min;
  if (cfg.nightStartMin < cfg.nightEndMin) return (m >= cfg.nightStartMin && m < cfg.nightEndMin);
  return (m >= cfg.nightStartMin || m < cfg.nightEndMin);  // window wraps midnight
}

// Draw a QR code (text encoded) as filled rects using the ESP32 core's
// built-in encoder (espressif__qrcode). Our strings are short (open-AP join
// string, short URL) → version 2-3 (25-29 modules); at scale 2 that's 50-58 px,
// fitting the 64 px-tall screen. The encoder is callback-based, so the draw
// origin/scale are passed via file-scope statics.
static int qrOx, qrOy, qrScale;
static bool qrRightAlign = false;
static int qrRightMargin = 2;
static void qrDisplayCb(esp_qrcode_handle_t qr) {
  int size = esp_qrcode_get_size(qr);
  // Right-aligned mode hugs the screen's right edge with a fixed margin,
  // adapting to the encoded QR's actual size so the left side is always free
  // for text (no overlap regardless of string length / QR version).
  int x0 = qrRightAlign ? (SCREEN_WIDTH - size * qrScale - qrRightMargin) : qrOx;
  if (x0 < 0) x0 = 0;
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++)
      if (esp_qrcode_get_module(qr, x, y))
        display.fillRect(x0 + x * qrScale, qrOy + y * qrScale, qrScale, qrScale, SSD1306_WHITE);
}
static void qrGenerate(const char* text) {
  esp_qrcode_config_t cfg = {};
  cfg.display_func = qrDisplayCb;
  cfg.max_qrcode_version = 4;  // bounds the encode buffer; our strings pick 2-3
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  esp_qrcode_generate(&cfg, text);
}
void drawQR(const char* text, int ox, int oy, int scale) {
  qrOx = ox; qrOy = oy; qrScale = scale; qrRightAlign = false;
  qrGenerate(text);
}
// Right-aligned variant: QR sits against the right edge with `margin` px to
// spare; `oy` is the top. Leaves the whole left side for text.
void drawQRRight(const char* text, int oy, int scale, int margin) {
  qrOy = oy; qrScale = scale; qrRightMargin = margin; qrRightAlign = true;
  qrGenerate(text);
  qrRightAlign = false;
}

void showPortalScreen() {
  if (!displayOK) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WiFi Setup");
  display.setCursor(0, 14);
  display.println("Scan QR ->");
  display.setCursor(0, 26);
  display.println("or join:");
  display.setCursor(0, 38);
  display.println(AP_SSID);
  display.setCursor(0, 52);
  display.println(AP_IP_STR);
  // QR for joining the open setup AP (right-aligned so it clears the text).
  // Format: WIFI:S:<ssid>;T:nopass;;
  drawQRRight("WIFI:S:" AP_SSID ";T:nopass;;", 2, 2, 3);
  display.display();
}

// Brief post-connect splash. No QR here — WiFi is already set on a normal
// reboot/OTA, so the QR (only useful for first setup / WiFi reset) lives on the
// captive-portal screen instead.
void showConnectedSplash() {
  if (!displayOK) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Connected!");
  display.setCursor(0, 18);
  display.println("Open dashboard at:");
  display.setCursor(0, 32);
  display.println(cfg.hostname + ".local");
  display.setCursor(0, 48);
  display.print("or  ");
  display.println(WiFi.localIP().toString());
  display.display();
}

void updateRunDisplay() {
  if (!displayOK || !displayPowered) return;  // skip while night-blanked
  // Two big data rows for legibility on the 0.96" screen: temp+humidity, then
  // pressure+IAQ (numbers size 2, units size 1). Calibration accuracy lives in
  // the web UI only. Anti-burn-in offset (0-2 px) shifts the whole layout.
  const int ox = shiftX, oy = shiftY;
  bool hdcGood = hdcOK && latest.hdcValid;
  bool bmeGood = bmeOK && latest.bmeValid;
  bool stale   = !hdcGood || !bmeGood;
  bool wifiUp  = (WiFi.status() == WL_CONNECTED);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ---- Header (size 1): name | version-or-STALE | status dot ----
  display.setTextSize(1);
  char nameBuf[12];
  snprintf(nameBuf, sizeof(nameBuf), "%.9s", cfg.devName.c_str());
  display.setCursor(ox, oy);
  display.print(nameBuf);
  char rbuf[12];
  if (stale) snprintf(rbuf, sizeof(rbuf), "STALE");          // unmistakable on-device alert
  else       snprintf(rbuf, sizeof(rbuf), "v%s", FW_VERSION);
  display.setCursor(114 - (int)strlen(rbuf) * 6 + ox, oy);
  display.print(rbuf);
  // Dot filled only when everything is healthy (WiFi + both sensors fresh), so a
  // solid dot can no longer mean "connected but no data".
  if (wifiUp && !stale) display.fillCircle(122 + ox, 3 + oy, 3, SSD1306_WHITE);
  else                  display.drawCircle(122 + ox, 3 + oy, 3, SSD1306_WHITE);
  display.drawFastHLine(ox, 11 + oy, SCREEN_WIDTH - 4, SSD1306_WHITE);

  // ---- Row 1 (size 2): temperature (left) + humidity (right-aligned) ----
  display.setTextSize(2);
  display.setCursor(ox, 15 + oy);
  if (hdcGood) display.printf("%.1f%c", toUnit(latest.hdcTempC), cfg.unit);
  else         display.printf("--.-%c", cfg.unit);
  char rh[8];
  if (hdcGood) snprintf(rh, sizeof(rh), "%.0f%%", latest.hdcRH);
  else         snprintf(rh, sizeof(rh), "--%%");
  display.setCursor(SCREEN_WIDTH - (int)strlen(rh) * 12 - 4 + ox, 15 + oy);
  display.print(rh);

  // ---- Row 2: pressure (big number + small "hPa") + IAQ (big number) ----
  char pb[8];
  if (bmeGood) snprintf(pb, sizeof(pb), "%.0f", latest.bmePresHpa);
  else         snprintf(pb, sizeof(pb), "--");
  display.setTextSize(2);
  display.setCursor(ox, 40 + oy);
  display.print(pb);
  int pEnd = (int)strlen(pb) * 12 + 2 + 18;   // end of "<pressure> hPa" block
  display.setTextSize(1);
  display.setCursor(ox + (int)strlen(pb) * 12 + 2, 46 + oy);
  display.print("hPa");

  char ib[6];
  if (bmeGood) snprintf(ib, sizeof(ib), "%.0f", latest.bmeIaq);
  else         snprintf(ib, sizeof(ib), "--");
  int ivX = SCREEN_WIDTH - (int)strlen(ib) * 12 - 4;  // big IAQ value, right-aligned
  display.setTextSize(2);
  display.setCursor(ivX + ox, 40 + oy);
  display.print(ib);
  // Adaptive label so it can never collide with "hPa": "IAQ" if it fits, else
  // "AQ", else nothing (4-digit pressure + 3-digit IAQ is the tight case).
  const char* aqLbl = "IAQ"; int aqW = 18;
  int gap = ivX - pEnd;
  if (gap < aqW + 4) { aqLbl = "AQ"; aqW = 12; }
  if (gap < aqW + 3) { aqLbl = "";   aqW = 0; }
  if (aqW) {
    display.setTextSize(1);
    display.setCursor(ivX - aqW - 2 + ox, 46 + oy);
    display.print(aqLbl);
  }

  display.display();
}

// =================== Web handlers ===================
// Human-readable cause of the most recent reset — stable for the whole run.
// "Software" covers the dashboard restart and a successful OTA (both reboot
// via software). Brownout points at power; Task WDT at a hang.
const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "Power-on";
    case ESP_RST_EXT:       return "External";
    case ESP_RST_SW:        return "Software";
    case ESP_RST_PANIC:     return "Panic";
    case ESP_RST_INT_WDT:   return "Int WDT";
    case ESP_RST_TASK_WDT:  return "Task WDT";
    case ESP_RST_WDT:       return "Other WDT";
    case ESP_RST_DEEPSLEEP: return "Deep sleep";
    case ESP_RST_BROWNOUT:  return "Brownout";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "Unknown";
  }
}

void buildDataJson(JsonDocument& doc) {
  SensorData s = latest;  // snapshot (cross-task; cosmetic tearing acceptable)
  doc["name"] = cfg.devName;
  doc["hostname"] = cfg.hostname;
  doc["unit"] = String(cfg.unit);
  doc["fw"] = FW_VERSION;
  doc["brightness"] = cfg.brightness;
  doc["night_en"] = cfg.nightEn;
  doc["night_start"] = cfg.nightStartMin;  // minutes-of-day
  doc["night_end"] = cfg.nightEndMin;
  doc["night_mode"] = cfg.nightMode;
  doc["tz"] = cfg.tzIndex;
  JsonObject cm = doc["comfort"].to<JsonObject>();
  cm["tmin"] = (int)lroundf(toUnit(cfg.comfortTminC));
  cm["tmax"] = (int)lroundf(toUnit(cfg.comfortTmaxC));
  cm["hmin"] = cfg.comfortHmin;
  cm["hmax"] = cfg.comfortHmax;
  if (hdcOK && s.hdcValid) {
    doc["temp"] = toUnit(s.hdcTempC);
    doc["rh"] = s.hdcRH;
  }
  if (bmeOK && s.bmeValid) {
    doc["pressure"] = s.bmePresHpa;
    doc["iaq"] = s.bmeIaq;
    doc["iaq_acc"] = s.bmeIaqAccuracy;
    doc["bme_temp"] = toUnit(s.bmeTempC);
    doc["bme_rh"] = s.bmeRH;
    doc["eco2"] = s.bmeEco2;
    doc["bvoc"] = s.bmeBvoc;
  }
  doc["hdc_ok"] = (hdcOK && s.hdcValid);
  doc["bme_ok"] = (bmeOK && s.bmeValid);
  doc["rssi"] = WiFi.RSSI();
  doc["ssid"] = WiFi.SSID();
  doc["ip"] = WiFi.localIP().toString();
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  // Heap health for the curl runbook (the small endpoint that stays reachable
  // when big pages stall). heap_min is the low-water mark since boot (catches a
  // slow leak); heap_largest is the biggest *contiguous* block — what a large
  // response actually needs, so it reveals fragmentation that "heap" alone hides.
  doc["heap_min"] = ESP.getMinFreeHeap();
  // Largest contiguous *internal* block — MALLOC_CAP_8BIT alone would report the
  // 2 MB PSRAM pool and hide internal-RAM fragmentation (the heap AsyncTCP pbufs
  // and small allocs live in). PSRAM is reported separately: big allocations land
  // there on this n4r2 board, which is why internal heap stayed flat in the
  // v1.3.6 stall — that was RF + payload size, not fragmentation.
  doc["heap_largest"] = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  doc["psram_free"] = ESP.getFreePsram();
  // ESP32-S3 internal die temperature (°C). Coarse, but lets the curl runbook
  // watch for thermal trouble — the "module too hot to touch" symptom in v1.3.12.
  doc["chip_temp"] = temperatureRead();
  doc["reset_reason"] = resetReasonName();
  if (s.hdcLastGoodMs > 0) doc["hdc_age"] = (millis() - s.hdcLastGoodMs) / 1000;
  if (s.bmeLastGoodMs > 0) doc["bme_age"] = (millis() - s.bmeLastGoodMs) / 1000;
#if ENABLE_MQTT
  doc["mqtt_enabled"] = true;
  doc["mqtt_host"] = cfg.mqttHost;
  doc["mqtt_user"] = cfg.mqttUser;
#endif
}

void buildHistoryJson(JsonDocument& doc) {
  doc["interval_s"] = HISTORY_INTERVAL_MS / 1000;
  doc["unit"] = String(cfg.unit);
  JsonArray at = doc["t"].to<JsonArray>();
  JsonArray arh = doc["rh"].to<JsonArray>();
  JsonArray ap = doc["p"].to<JsonArray>();
  JsonArray ai = doc["iaq"].to<JsonArray>();
  // Chart shows only the most recent 24 h (CHART_SAMPLES); the full 7-day buffer
  // is reserved for the CSV export. Emit oldest -> newest. ArduinoJson maps NaN
  // to JSON null (ARDUINOJSON_ENABLE_NAN == 0), so gaps come through cleanly.
  int n = histCount < CHART_SAMPLES ? histCount : CHART_SAMPLES;
  for (int k = 0; k < n; k++) {
    int idx = (histHead - n + k + HISTORY_SAMPLES * 2) % HISTORY_SAMPLES;
    at.add(toUnit(histT[idx]));   // toUnit(NaN) == NaN -> null
    arh.add(histRH[idx]);
    ap.add(histP[idx]);
    ai.add(histIAQ[idx]);
  }
}

// One history sample -> CSV row in `out` (size >= 64); returns the byte length.
// Empty fields for NaN gaps and pre-clock-sync (ts==0) timestamps. Reads only the
// ring buffer, so it's safe to call from the async web task (no flash/I2C/blocking).
static int histCsvRow(int idx, char* out, size_t sz) {
  int n = 0;
  uint32_t ts = histTime[idx];
  if (ts > 0) {
    time_t t = (time_t)ts;
    struct tm tmv;
    localtime_r(&t, &tmv);
    n += strftime(out + n, sz - n, "%Y-%m-%d %H:%M:%S", &tmv);
  }
  out[n++] = ',';
  if (!isnan(histT[idx]))   n += snprintf(out + n, sz - n, "%.2f", toUnit(histT[idx]));
  out[n++] = ',';
  if (!isnan(histRH[idx]))  n += snprintf(out + n, sz - n, "%.1f", histRH[idx]);
  out[n++] = ',';
  if (!isnan(histP[idx]))   n += snprintf(out + n, sz - n, "%.1f", histP[idx]);
  out[n++] = ',';
  if (!isnan(histIAQ[idx])) n += snprintf(out + n, sz - n, "%.0f", histIAQ[idx]);
  out[n++] = '\n';
  return n;
}

void registerRunRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    // Serve the dashboard gzip-compressed and zero-copy: the (uint8_t*, len)
    // overload maps to AsyncProgmemResponse, which streams straight from flash
    // in ~1.4 KB TCP segments (memcpy_P) with no heap String. The old
    // const-char* overload copied the whole ~35 KB page into a contiguous heap
    // String per request — which stalled out on a fragmented heap / weak link.
    // Gzip (~35 KB -> ~9 KB) also means far fewer segments to lose on a marginal
    // WiFi link. Cache-Control lets the browser skip the re-fetch on refresh.
    AsyncWebServerResponse* res =
      req->beginResponse(200, "text/html", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
    res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "max-age=86400");
    req->send(res);
  });
  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    buildDataJson(doc);
    AsyncResponseStream* res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });
  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    buildHistoryJson(doc);
    AsyncResponseStream* res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });
  // CSV export — the full 7-day buffer. Timestamps are in the device's local
  // time (per the timezone setting). NaN gaps and pre-clock-sync samples come
  // through as empty fields.
  server.on("/api/history.csv", HTTP_GET, [](AsyncWebServerRequest* req) {
    // Chunked, NOT buffered. AsyncResponseStream would grow ONE contiguous heap
    // buffer to hold the whole ~80 KB CSV before sending a byte — the same
    // large-contiguous-allocation failure mode as the old dashboard String copy.
    // beginChunkedResponse asks this filler for ~1.4 KB at a time, so peak RAM is
    // a single TCP segment regardless of how many samples are stored. The filler
    // runs in the async task and only reads the ring buffer (snapshotted below so
    // a mid-export sample write can't shift the indices), emitting whole rows only
    // so a row is never split across chunks.
    AsyncWebServerResponse* res = req->beginChunkedResponse("text/csv",
      [head = histHead, count = histCount, unit = cfg.unit, headerDone = false, row = 0]
      (uint8_t* buf, size_t maxLen, size_t) mutable -> size_t {
        size_t pos = 0;
        char line[64];
        if (!headerDone) {
          int n = snprintf(line, sizeof(line),
                           "timestamp,temp_%c,humidity_pct,pressure_hpa,iaq\n", unit);
          if ((size_t)n > maxLen) return 0;  // can't happen (MSS >> header)
          memcpy(buf, line, n); pos += n; headerDone = true;
        }
        while (row < count) {
          int idx = (head - count + row + HISTORY_SAMPLES * 2) % HISTORY_SAMPLES;
          int n = histCsvRow(idx, line, sizeof(line));
          if (pos + (size_t)n > maxLen) break;  // won't fit; resume next call
          memcpy(buf + pos, line, n); pos += n; row++;
        }
        return pos;  // 0 only once header + all rows are out -> ends the response
      });
    res->addHeader("Content-Disposition", "attachment; filename=airbox-history-7d.csv");
    req->send(res);
  });
  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest* req) {
    auto P = [&](const char* k) -> String {
      return req->hasParam(k, true) ? req->getParam(k, true)->value() : String("");
    };
    stgName = P("name"); stgUnit = P("unit"); stgHost = P("hostname"); stgPass = P("pass");
    stgBright = P("brightness"); stgNight = P("night_en");
    stgNStart = P("night_start"); stgNEnd = P("night_end");
    stgNMode = P("night_mode"); stgTz = P("tz");
    stgCtmin = P("comfort_tmin"); stgCtmax = P("comfort_tmax");
    stgChmin = P("comfort_hmin"); stgChmax = P("comfort_hmax"); stgCunit = P("comfort_unit");
#if ENABLE_MQTT
    stgMqttHost = P("mqtt_host"); stgMqttUser = P("mqtt_user"); stgMqttPass = P("mqtt_pass");
#endif
    pendingSaveSettings = true;
    req->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* req) {
    pendingRestart = true; req->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/recalibrate", HTTP_POST, [](AsyncWebServerRequest* req) {
    pendingRecalibrate = true; req->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/reconfigure", HTTP_POST, [](AsyncWebServerRequest* req) {
    pendingReconfigure = true; req->send(200, "application/json", "{\"ok\":true}");
  });
  server.onNotFound([](AsyncWebServerRequest* req) { req->send(404, "text/plain", "Not found"); });
}

void sendScanJson(AsyncWebServerRequest* req) {
  int n = WiFi.scanComplete();
  JsonDocument doc;
  if (n == WIFI_SCAN_RUNNING || n == WIFI_SCAN_FAILED) {
    doc["scanning"] = true;
  } else {
    doc["scanning"] = false;
    JsonArray arr = doc["networks"].to<JsonArray>();
    for (int i = 0; i < n && i < 20; i++) {
      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = WiFi.SSID(i);
      o["rssi"] = WiFi.RSSI(i);
      o["enc"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
  }
  AsyncResponseStream* res = req->beginResponseStream("application/json");
  serializeJson(doc, *res);
  req->send(res);
}

void registerPortalRoutes() {
  auto portal = [](AsyncWebServerRequest* req) { req->send(200, "text/html", PORTAL_HTML); };
  server.on("/", HTTP_GET, portal);
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest* req) { sendScanJson(req); });
  server.on("/rescan", HTTP_GET, [](AsyncWebServerRequest* req) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true /*async*/, true /*show hidden*/);
    req->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    stgSsid = req->hasParam("ssid", true) ? req->getParam("ssid", true)->value() : String("");
    stgWifiPass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : String("");
    req->send(200, "application/json", "{\"ok\":true}");
    pendingSaveWifi = true;
  });
  // OS captive-portal probes -> redirect into the portal so the sheet pops.
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->redirect(String("http://") + AP_IP_STR + "/");
  });
  server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->redirect(String("http://") + AP_IP_STR + "/");
  });
  server.on("/hotspot-detect.html", HTTP_GET, portal);              // Apple
  server.on("/library/test/success.html", HTTP_GET, portal);        // Apple
  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* req) { // Windows
    req->redirect(String("http://") + AP_IP_STR + "/");
  });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->redirect(String("http://") + AP_IP_STR + "/");
  });
  server.onNotFound([](AsyncWebServerRequest* req) {
    req->redirect(String("http://") + AP_IP_STR + "/");
  });
}

// =================== OTA (browser via ElegantOTA) ===================
void setupElegantOTA() {
  // Auth + callbacks before begin() so they apply to the mounted /update route.
  ElegantOTA.setAuth("admin", cfg.adminPass.c_str());
  ElegantOTA.onStart([]() {
    wdtReconfigure(OTA_WATCHDOG_MS);  // extend WDT for the flash duration
    Serial.println("OTA: starting (WDT extended)");
  });
  ElegantOTA.onEnd([](bool success) {
    if (!success) {
      wdtReconfigure(WATCHDOG_TIMEOUT_MS);  // restore on failure (success reboots)
      Serial.println("OTA: failed — WDT restored");
    } else {
      Serial.println("OTA: complete, rebooting");
    }
  });
  ElegantOTA.begin(&server);
}

#if ENABLE_ARDUINO_OTA
void setupArduinoOTA() {
  ArduinoOTA.setHostname(cfg.hostname.c_str());
  ArduinoOTA.setPassword(cfg.adminPass.c_str());
  ArduinoOTA.onStart([]() { wdtReconfigure(OTA_WATCHDOG_MS); });
  ArduinoOTA.onError([](ota_error_t) { wdtReconfigure(WATCHDOG_TIMEOUT_MS); });
  ArduinoOTA.begin();
}
#endif

// =================== MQTT (optional, preserved) ===================
#if ENABLE_MQTT
void publishOneDiscovery(const char* component, const char* objectId, const char* friendlyName,
                         const char* unit, const char* deviceClass, const char* valueKey,
                         uint8_t precision, bool isDiagnostic = false, bool isBinary = false,
                         bool isText = false) {
  String configTopic = String(DISCOVERY_PREFIX) + "/" + component + "/" + deviceId + "_" + objectId + "/config";
  JsonDocument doc;
  doc["name"] = friendlyName;
  doc["unique_id"] = deviceId + "_" + objectId;
  doc["object_id"] = roomSlug + "_" + objectId;
  doc["state_topic"] = stateTopic;
  doc["availability_topic"] = availTopic;
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";
  doc["value_template"] = String("{{ value_json.") + valueKey + " }}";
  if (isBinary) { doc["payload_on"] = "1"; doc["payload_off"] = "0"; }
  else if (!isText) {
    if (strlen(unit) > 0) doc["unit_of_measurement"] = unit;
    doc["state_class"] = "measurement";
    doc["suggested_display_precision"] = precision;
  }
  if (strlen(deviceClass) > 0) doc["device_class"] = deviceClass;
  if (isDiagnostic) doc["entity_category"] = "diagnostic";
  JsonObject device = doc["device"].to<JsonObject>();
  JsonArray ids = device["identifiers"].to<JsonArray>();
  ids.add(deviceId);
  device["name"] = haDeviceName;
  device["manufacturer"] = "SotaGoat Labs";
  device["model"] = DEVICE_MODEL;
  device["sw_version"] = FW_VERSION;
  char payload[768];
  size_t nn = serializeJson(doc, payload, sizeof(payload));
  mqtt.publish(configTopic.c_str(), (uint8_t*)payload, nn, true);
}

void publishDiscovery() {
  if (hdcOK) {
    publishOneDiscovery("sensor", "hdc_temp", "Temperature", "°C", "temperature", "hdc_temp_c", 1);
    publishOneDiscovery("sensor", "hdc_rh", "Humidity", "%", "humidity", "hdc_rh", 0);
  }
  if (bmeOK) {
    publishOneDiscovery("sensor", "bme_pres", "Pressure", "hPa", "atmospheric_pressure", "bme_pres_hpa", 1);
    publishOneDiscovery("sensor", "bme_iaq", "IAQ", "", "", "bme_iaq", 0);
    publishOneDiscovery("sensor", "bme_eco2", "eCO2", "ppm", "carbon_dioxide", "bme_eco2", 0);
    publishOneDiscovery("sensor", "bme_iaq_accuracy", "IAQ Calibration", "", "", "bme_iaq_accuracy", 0, true);
  }
  publishOneDiscovery("sensor", "rssi", "WiFi RSSI", "dBm", "signal_strength", "rssi", 0, true);
  publishOneDiscovery("sensor", "uptime", "Uptime", "s", "duration", "uptime", 0, true);
}

void tryConnectMQTT() {
  if (mqtt.connected() || cfg.mqttHost.length() == 0) return;
  bool ok = mqtt.connect(deviceId.c_str(), cfg.mqttUser.c_str(), cfg.mqttPass.c_str(),
                         availTopic.c_str(), 1, true, "offline");
  if (ok) {
    mqtt.publish(availTopic.c_str(), "online", true);
    publishDiscovery();
  } else {
    Serial.printf("MQTT failed, state=%d\n", mqtt.state());
  }
}

void publishState() {
  JsonDocument doc;
  if (hdcOK && latest.hdcValid) { doc["hdc_temp_c"] = latest.hdcTempC; doc["hdc_rh"] = latest.hdcRH; }
  if (bmeOK && latest.bmeValid) {
    doc["bme_pres_hpa"] = latest.bmePresHpa;
    doc["bme_iaq"] = round(latest.bmeIaq);
    doc["bme_eco2"] = round(latest.bmeEco2);
    doc["bme_iaq_accuracy"] = latest.bmeIaqAccuracy;
  }
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;
  char payload[384];
  size_t nn = serializeJson(doc, payload, sizeof(payload));
  mqtt.publish(stateTopic.c_str(), (uint8_t*)payload, nn, false);
}

void setupMqtt() {
  haDeviceName = cfg.devName;
  roomSlug = cfg.devName; roomSlug.toLowerCase(); roomSlug.replace(' ', '_');
  stateTopic = String("env/") + deviceId + "/state";
  availTopic = String("env/") + deviceId + "/availability";
  mqtt.setServer(cfg.mqttHost.c_str(), MQTT_PORT);
  mqtt.setBufferSize(1024);
  mqtt.setSocketTimeout(5);
}
#endif  // ENABLE_MQTT

// =================== Sensor init ===================
// Recover a wedged Wire1 bus: if a device is mid-transfer it can hold SDA low,
// which a plain Wire1.begin() won't clear (a soft reboot, e.g. after OTA, leaves
// devices powered and possibly stuck). Manually clock SCL until SDA releases,
// emit a STOP, then re-init the peripheral. Cheap insurance, runs on boot and
// before each reinit attempt.
void recoverI2C() {
  Wire1.end();
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(I2C_SCL_PIN, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SCL_PIN, HIGH);
  for (int i = 0; i < 9 && digitalRead(I2C_SDA_PIN) == LOW; i++) {
    digitalWrite(I2C_SCL_PIN, LOW);  delayMicroseconds(5);
    digitalWrite(I2C_SCL_PIN, HIGH); delayMicroseconds(5);
  }
  // STOP condition: SDA low->high while SCL is high.
  pinMode(I2C_SDA_PIN, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SDA_PIN, LOW);  delayMicroseconds(5);
  digitalWrite(I2C_SCL_PIN, HIGH); delayMicroseconds(5);
  digitalWrite(I2C_SDA_PIN, HIGH); delayMicroseconds(5);
  Wire1.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire1.setClock(100000);
  Wire1.setTimeOut(50);
}

void initSensors() {
  recoverI2C();  // start from a known-good bus (also does Wire1.begin)
  Wire1.setClock(100000);
  Wire1.setTimeOut(50);

  displayOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  if (!displayOK) displayOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (displayOK) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Booting...");
    display.display();
    Serial.println("OLED detected");
  } else {
    Serial.println("No OLED on bus, running headless");
  }

  if (envSensor.begin(BME68X_I2C_ADDR_HIGH, Wire1)) {
    envSensor.attachCallback(bsecDataCallback);
    loadBsecState();
    envSensor.updateSubscription(const_cast<bsecSensor*>(bsecSubscriptionList),
                                 bsecSubscriptionCount, BSEC_SAMPLE_RATE_LP);
    if ((int)envSensor.status >= 0) {
      bmeOK = true;
      Serial.println("BME688/BSEC initialized at LP (3 s)");
    } else {
      Serial.println("BSEC subscription error (will retry every 5 min)");
      logBsecStatus("setup updateSubscription");
    }
  } else {
    Serial.println("BME688 init failed (will retry every 5 min)");
    logBsecStatus("setup begin");
  }

  hdcOK = hdc.begin(0x44, &Wire1);
  if (!hdcOK) Serial.println("HDC3022 init failed (will retry every 5 min)");
}

// =================== WiFi bring-up ===================
bool connectSTA() {
  Serial.printf("WiFi connecting to %s...\n", cfg.ssid.c_str());
  if (displayOK) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.printf("WiFi:\n%s", cfg.ssid.c_str());
    display.display();
  }
  WiFi.mode(WIFI_STA);
  // Keep WiFi modem power-save ON (default WIFI_PS_MIN_MODEM): the radio dozes
  // between DTIM beacons, cutting steady-state radio power and heat a lot. v1.3.7
  // disabled it to fight web lag, but that lag was really the 35 KB page (now
  // 12 KB gzipped) + buffered CSV (now chunked) — and leaving the radio at full
  // power continuously cooked the S3 module on a weak link (constant max-TX
  // retransmits). The small added latency is fine for this dashboard.
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
    delay(500);
    Serial.print(".");
    esp_task_wdt_reset();
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi OK, IP: %s, RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  Serial.println("\nWiFi connect timed out");
  return false;
}

void startPortal() {
  mode = MODE_PORTAL;
  Serial.println("Starting captive setup portal (AP: " AP_SSID ")");
  // AP_STA so the portal can scan for the user's networks; we never auto-
  // associate STA here, so the AP stays stable apart from brief scan blips.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID);  // open network — see config.h rationale
  WiFi.scanNetworks(true, true);  // kick off async scan for the portal list
  dnsServer.start(53, "*", apIP);
  registerPortalRoutes();
  setupElegantOTA();  // allow recovery flashing from the portal too
  server.begin();
  showPortalScreen();
  Serial.println("Portal ready at http://" AP_IP_STR);
}

void startRun() {
  mode = MODE_RUN;
  if (!MDNS.begin(cfg.hostname.c_str()))
    Serial.println("mDNS start failed");
  else
    Serial.printf("mDNS: http://%s.local\n", cfg.hostname.c_str());
  registerRunRoutes();
  setupElegantOTA();
  server.begin();
  MDNS.addService("http", "tcp", 80);
  // NTP for the night-mode clock (harmless if night mode stays off).
  applyTimezone();
#if ENABLE_ARDUINO_OTA
  setupArduinoOTA();
#endif
#if ENABLE_MQTT
  setupMqtt();
  tryConnectMQTT();
#endif
  Serial.println("Dashboard up");
}

// =================== Pending-action handler (loop context) ===================
void handlePendingActions() {
  // Every branch below that reboots flushes history first, so a planned restart
  // never loses recent trend points.
  if (pendingFactoryReset || pendingReconfigure || pendingSaveWifi
      || pendingRecalibrate || pendingRestart) {
    saveHistory();
  }
  if (pendingFactoryReset || pendingReconfigure) {
    Serial.println("Wiping WiFi creds, rebooting into portal");
    if (displayOK) {
      display.clearDisplay(); display.setCursor(0, 0);
      display.println("WiFi reset.\nRebooting to\nsetup portal..."); display.display();
    }
    clearWifiCreds();
    delay(800);
    ESP.restart();
  }
  if (pendingSaveWifi) {
    pendingSaveWifi = false;
    Serial.printf("Saving WiFi creds for %s, rebooting\n", stgSsid.c_str());
    saveWifiCreds(stgSsid, stgWifiPass);
    delay(800);
    ESP.restart();
  }
  if (pendingRecalibrate) {
    pendingRecalibrate = false;
    Serial.println("Clearing BSEC calibration, rebooting");
    bsecPrefs.begin("bsec", false);
    bsecPrefs.clear();
    bsecPrefs.end();
    delay(500);
    ESP.restart();
  }
  if (pendingSaveSettings) {
    pendingSaveSettings = false;
    applyAndSaveSettings();
  }
  if (pendingRestart) {
    pendingRestart = false;
    Serial.println("Restart requested");
    delay(500);
    ESP.restart();
  }
}

// =================== BOOT-button factory reset ===================
// Read post-boot only (GPIO0 is a strapping pin — must NOT be low at reset).
// A sustained low for FACTORY_RESET_HOLD_MS wipes WiFi creds.
void checkBootButton() {
  static unsigned long pressStart = 0;
  static bool wasLow = false;
  static bool fired = false;
  bool low = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  if (low && !wasLow) { pressStart = millis(); wasLow = true; fired = false; }
  else if (low && wasLow && !fired) {
    if (millis() - pressStart >= FACTORY_RESET_HOLD_MS) {
      fired = true;
      pendingFactoryReset = true;
    }
  } else if (!low) {
    wasLow = false;
  }
}

// =================== setup ===================
void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.printf("\nAirBox v%s booting...\n", FW_VERSION);

  // Watchdog first so everything below is protected.
  esp_task_wdt_config_t wdtConfig = {.timeout_ms = WATCHDOG_TIMEOUT_MS, .idle_core_mask = 0, .trigger_panic = true};
  esp_err_t wdtErr = esp_task_wdt_init(&wdtConfig);
  if (wdtErr == ESP_ERR_INVALID_STATE) esp_task_wdt_reconfigure(&wdtConfig);
  else if (wdtErr != ESP_OK) Serial.printf("WDT init returned %d (continuing)\n", wdtErr);
  esp_task_wdt_add(NULL);

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  initSensors();

  // Stable device ID from MAC (eFuse-direct, no WiFi dependency).
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[16];
  snprintf(buf, sizeof(buf), "qtpy_%02x%02x%02x", mac[3], mac[4], mac[5]);
  deviceId = String(buf);

  loadConfig();
  if (displayOK) setContrast(cfg.brightness);  // apply saved brightness early

  // Mount LittleFS and restore the trend history so charts survive a reboot.
  fsOK = LittleFS.begin(true);  // format on first boot if unformatted
  if (fsOK) { loadHistory(); }
  else      { Serial.println("LittleFS mount failed — history won't persist"); }

  Serial.printf("Device: %s  host: %s.local  unit: %c\n",
                cfg.devName.c_str(), cfg.hostname.c_str(), cfg.unit);

  // Decide mode: try STA if we have creds; otherwise (or on failure) portal.
  if (cfg.ssid.length() > 0 && connectSTA()) {
    startRun();
    showConnectedSplash();  // brief address splash, then readings take over
    unsigned long splash = millis();
    while (millis() - splash < 8000) { esp_task_wdt_reset(); delay(100); }
    readSensors();
    updateRunDisplay();
    lastHistMs = millis();
  } else {
    startPortal();
  }
}

// =================== loop ===================
void loop() {
  esp_task_wdt_reset();
  ElegantOTA.loop();
  checkBootButton();
  handlePendingActions();

  if (mode == MODE_PORTAL) {
    dnsServer.processNextRequest();
    static unsigned long lastPortalDraw = 0;
    if (millis() - lastPortalDraw > 5000) { lastPortalDraw = millis(); showPortalScreen(); }
    return;
  }

  // ---- MODE_RUN ----
#if ENABLE_ARDUINO_OTA
  ArduinoOTA.handle();
#endif

  if (bmeOK) {
    if (!envSensor.run()) {
      bool realError = ((int)envSensor.status < 0) || ((int)envSensor.sensor.status < 0);
      if (realError) {
        latest.bmeFailStreak++;
        latest.bmeTotalFails++;
        Serial.printf("BSEC run failed (streak=%u, total=%lu)\n",
                      latest.bmeFailStreak, (unsigned long)latest.bmeTotalFails);
        logBsecStatus("run");
      }
    }
  }

  // WiFi: trust auto-reconnect; force a full cycle if down too long.
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiReconnect > WIFI_FORCE_RECONNECT_MS) {
      lastWifiReconnect = millis();
      Serial.println("WiFi still down — forcing reconnect");
      WiFi.disconnect();
      WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    }
  } else {
    lastWifiReconnect = millis();
  }

#if ENABLE_MQTT
  if (!mqtt.connected() && millis() - lastMqttAttempt > MQTT_RETRY_MS) {
    lastMqttAttempt = millis();
    if (WiFi.status() == WL_CONNECTED) tryConnectMQTT();
  }
  if (mqtt.connected()) mqtt.loop();
#endif

  unsigned long now = millis();

  // Per-sensor staleness (preserved).
  bool hdcJustWentStale = false, bmeJustWentStale = false;
  if (hdcOK && latest.hdcValid && (now - latest.hdcLastGoodMs > MAX_VALUE_AGE_MS)) {
    Serial.println("HDC stale — marking offline");
    hdcOK = false; latest.hdcValid = false; hdcJustWentStale = true;
  }
  if (bmeOK && latest.bmeValid && (now - latest.bmeLastGoodMs > MAX_VALUE_AGE_MS)) {
    Serial.println("BME stale — marking offline");
    bmeOK = false; latest.bmeValid = false; bmeJustWentStale = true;
  }
  if (hdcOK && latest.hdcFailStreak > MAX_FAIL_STREAK) {
    Serial.printf("HDC failed %u reads — forcing reinit\n", latest.hdcFailStreak);
    hdcOK = false; latest.hdcValid = false; latest.hdcFailStreak = 0; hdcJustWentStale = true;
  }
  if (bmeOK && latest.bmeFailStreak > MAX_FAIL_STREAK) {
    Serial.printf("BME failed %u reads — forcing reinit\n", latest.bmeFailStreak);
    bmeOK = false; latest.bmeValid = false; latest.bmeFailStreak = 0; bmeJustWentStale = true;
  }
  bool hdcDue = (!hdcOK && (hdcJustWentStale || (now - lastHdcReinit >= SENSOR_REINIT_INTERVAL_MS)));
  bool bmeDue = (!bmeOK && (bmeJustWentStale || (now - lastBmeReinit >= SENSOR_REINIT_INTERVAL_MS)));
  if (hdcDue || bmeDue) recoverI2C();  // clear a wedged bus before re-init
  if (hdcDue) { lastHdcReinit = now; reinitHdc(); }
  if (bmeDue) { lastBmeReinit = now; reinitBme(); }

  // OLED care: night blank/dim + anti-burn-in pixel-shift.
  if (displayOK) {
    bool night = isNightNow();
    bool wasPowered = displayPowered;
    // BLANK mode powers the panel off; DIM mode keeps it on at low contrast.
    setDisplayPower(!(night && cfg.nightMode == 0));
    setContrast((night && cfg.nightMode == 1) ? NIGHT_DIM_CONTRAST : cfg.brightness);
    if (displayPowered && !wasPowered) updateRunDisplay();  // redraw on wake
    if (displayPowered && now - lastShift >= PIXEL_SHIFT_INTERVAL_MS) {
      lastShift = now;
      updateShift();
    }
  }

  unsigned long readInterval = displayOK ? READ_INTERVAL_DISPLAY_MS : READ_INTERVAL_HEADLESS_MS;
  if (now - lastRead >= readInterval) {
    lastRead = now;
    readSensors();
    updateRunDisplay();
  }

  if (now - lastHistMs >= HISTORY_INTERVAL_MS) {
    lastHistMs = now;
    recordHistory();
  }

  // Flush history to flash on a slow cadence (planned restarts flush too).
  if (fsOK && now - lastHistSave >= HISTORY_SAVE_INTERVAL_MS) {
    lastHistSave = now;
    saveHistory();
  }

#if ENABLE_MQTT
  if (now - lastPublish >= PUBLISH_INTERVAL_MS && mqtt.connected()) {
    lastPublish = now;
    publishState();
  }
#endif

  // Persist BSEC calibration once accuracy >= 1 (preserved).
  if (bmeOK && latest.bmeIaqAccuracy >= 1) {
    bool firstSave = !bsecStateSavedOnce;
    bool cadenceDue = (now - lastBsecStateSave >= BSEC_STATE_SAVE_INTERVAL_MS);
    if (firstSave || cadenceDue) {
      lastBsecStateSave = now;
      bsecStateSavedOnce = true;
      saveBsecState();
    }
  }

  // Yield the CPU briefly so the loop core can idle (WFI) between ticks instead
  // of spinning at 100%. Big steady-state heat/power cut on an always-on board;
  // the async web server runs in its own task and is unaffected, and BSEC LP
  // (3 s cadence) and the WDT (60 s) are nowhere near starved at ~100 loops/s.
  delay(10);
}
