// Rumah Pompa — ESP32 well / tank / irrigation controller.
//
// Responsibilities:
//   * Refill the storage tank from the well, with dry-run protection.
//   * Water the garden on two daily schedules, or on demand from the app.
//   * Switch two garden lights and two spare loads from the app.
//   * Keep running the local pump logic when the internet is down.
//
// The decision logic itself lives in water_logic.cpp, which has no Arduino
// dependency and is unit-tested on the host (test/run_tests.sh). This file is
// the hardware and connectivity layer wrapped around it.
//
// Everything Blynk-related stays in this one file on purpose: BlynkSimpleEsp32.h
// *defines* the global `Blynk` object, so including it from a second translation
// unit would produce a duplicate-symbol error at link time.

// Must be defined before any Blynk header is pulled in.
#define BLYNK_NO_YIELD             // required on ESP32 Arduino core 3.x
#define BLYNK_PRINT Serial         // Blynk diagnostics on the serial monitor

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing secrets.h — copy RumahPompa/secrets.example.h to RumahPompa/secrets.h and fill in your credentials."
#endif

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <esp_task_wdt.h>
#include <time.h>

#include "config.h"
#include "water_logic.h"

// Declared up front so the sketch reads top-down. These are ordinary file-scope
// functions rather than a `namespace {}` block, because the Arduino IDE's
// automatic prototype generator does not handle functions nested in namespaces.
void applySchedule(const BlynkParam& param, water::WateringWindow& window,
                   int defaultDuration, const char* label);
void runControlPass();
void publishTelemetry(const water::SensorState& sensors);
void maintainWifi();

// ---------------------------------------------------------------------------
// Runtime state
// ---------------------------------------------------------------------------
water::WateringWindow morningWindow = {
    DEFAULT_MORNING_HOUR * 60 + DEFAULT_MORNING_MINUTE,
    DEFAULT_MORNING_DURATION_MIN};
water::WateringWindow eveningWindow = {
    DEFAULT_EVENING_HOUR * 60 + DEFAULT_EVENING_MINUTE,
    DEFAULT_EVENING_DURATION_MIN};

water::ManualOverrides manualOverrides = {false, false};
water::OutputState outputs = {false, false, false};

BlynkTimer timer;

unsigned long lastWifiAttemptMs = 0;
unsigned long lastTelemetryRefreshMs = 0;

// Last values pushed to the app, so unchanged readings are not re-sent on every
// pass. Blynk rate-limits virtual writes, and a full refresh every two seconds
// spends that budget for no benefit.
water::OutputState lastSentOutputs = {false, false, false};
const char* lastSentWellStatus = nullptr;
const char* lastSentTankStatus = nullptr;

// ---------------------------------------------------------------------------
// Blynk: manual controls
// ---------------------------------------------------------------------------
BLYNK_WRITE(VPIN_CONTROL_SUBMERSIBLE) { manualOverrides.submersible = param.asInt() == 1; }
BLYNK_WRITE(VPIN_CONTROL_IRRIGATION) { manualOverrides.irrigation = param.asInt() == 1; }

BLYNK_WRITE(VPIN_CONTROL_LIGHT_INNER) {
  digitalWrite(PIN_RELAY_LIGHT_INNER, param.asInt() == 1 ? RELAY_ON : RELAY_OFF);
}
BLYNK_WRITE(VPIN_CONTROL_LIGHT_OUTER) {
  digitalWrite(PIN_RELAY_LIGHT_OUTER, param.asInt() == 1 ? RELAY_ON : RELAY_OFF);
}
BLYNK_WRITE(VPIN_CONTROL_SPARE_1) {
  digitalWrite(PIN_RELAY_SPARE_1, param.asInt() == 1 ? RELAY_ON : RELAY_OFF);
}
BLYNK_WRITE(VPIN_CONTROL_SPARE_2) {
  digitalWrite(PIN_RELAY_SPARE_2, param.asInt() == 1 ? RELAY_ON : RELAY_OFF);
}

// ---------------------------------------------------------------------------
// Blynk: watering schedules
//
// The TimeInput widget sends a start and, when configured as a range, a stop
// time. The stop time sets the run length; without one the built-in default
// duration stands.
// ---------------------------------------------------------------------------
void applySchedule(const BlynkParam& param, water::WateringWindow& window,
                   int defaultDuration, const char* label) {
  TimeInputParam t(param);
  if (!t.hasStartTime()) {
    return;
  }
  window.startMinute = t.getStartHour() * 60 + t.getStartMinute();

  if (t.hasStopTime()) {
    const int stopMinute = t.getStopHour() * 60 + t.getStopMinute();
    const int duration = water::windowDuration(window.startMinute, stopMinute);
    window.durationMinutes = duration > 0 ? duration : defaultDuration;
  } else {
    window.durationMinutes = defaultDuration;
  }

  Serial.printf("[SCHEDULE] %s watering: %02d:%02d for %d min\n", label,
                window.startMinute / 60, window.startMinute % 60,
                window.durationMinutes);
}

BLYNK_WRITE(VPIN_SCHEDULE_MORNING) {
  applySchedule(param, morningWindow, DEFAULT_MORNING_DURATION_MIN, "Morning");
}
BLYNK_WRITE(VPIN_SCHEDULE_EVENING) {
  applySchedule(param, eveningWindow, DEFAULT_EVENING_DURATION_MIN, "Evening");
}

// ---------------------------------------------------------------------------
// Blynk: reconnection
//
// After a reboot every relay is off, but the app still shows whatever the
// switches were last left at. Pulling those values back puts the two in step
// instead of leaving the app lying about the hardware.
// ---------------------------------------------------------------------------
BLYNK_CONNECTED() {
  Blynk.syncVirtual(VPIN_CONTROL_SUBMERSIBLE, VPIN_CONTROL_IRRIGATION,
                    VPIN_CONTROL_LIGHT_INNER, VPIN_CONTROL_LIGHT_OUTER,
                    VPIN_CONTROL_SPARE_1, VPIN_CONTROL_SPARE_2,
                    VPIN_SCHEDULE_MORNING, VPIN_SCHEDULE_EVENING);
  lastTelemetryRefreshMs = 0;  // force a full status push on the next pass
}

// ---------------------------------------------------------------------------
// Sensors and telemetry
// ---------------------------------------------------------------------------

// A probe pulls its pin to ground when submerged; the internal pull-up holds it
// high when dry.
bool probeSubmerged(int pin) { return digitalRead(pin) == LOW; }

const char* wellStatusText(const water::SensorState& s) {
  if (s.wellEmpty) return "SUMUR KERING (PROTEKSI)";
  if (s.wellFull) return "SUMUR PENUH / AMAN";
  return "PENGISIAN DEBIT AIR";
}

const char* tankStatusText(const water::SensorState& s) {
  if (s.tankFull) return "PENUH (100%)";
  if (s.tankEmpty) return "KOSONG / REFILL (0%)";
  return "TERISI SEBAGIAN (MID)";
}

void publishTelemetry(const water::SensorState& sensors) {
  if (WiFi.status() != WL_CONNECTED || !Blynk.connected()) {
    return;
  }

  const unsigned long now = millis();
  const bool forceRefresh = lastTelemetryRefreshMs == 0 ||
                            (now - lastTelemetryRefreshMs) >= TELEMETRY_REFRESH_MS;
  if (forceRefresh) {
    lastTelemetryRefreshMs = now;
  }

  if (forceRefresh || outputs.submersiblePump != lastSentOutputs.submersiblePump) {
    Blynk.virtualWrite(VPIN_MONITOR_SUBMERSIBLE, outputs.submersiblePump ? 255 : 0);
  }
  if (forceRefresh || outputs.irrigationPump != lastSentOutputs.irrigationPump) {
    Blynk.virtualWrite(VPIN_MONITOR_IRRIGATION, outputs.irrigationPump ? 255 : 0);
  }
  if (forceRefresh || outputs.solenoid != lastSentOutputs.solenoid) {
    Blynk.virtualWrite(VPIN_MONITOR_SOLENOID, outputs.solenoid ? 255 : 0);
  }
  lastSentOutputs = outputs;

  // These compare pointers rather than strings on purpose: each helper returns
  // one of a fixed set of literals, so the address identifies the message.
  const char* wellText = wellStatusText(sensors);
  if (forceRefresh || wellText != lastSentWellStatus) {
    Blynk.virtualWrite(VPIN_STATUS_WELL, wellText);
    lastSentWellStatus = wellText;
  }

  const char* tankText = tankStatusText(sensors);
  if (forceRefresh || tankText != lastSentTankStatus) {
    Blynk.virtualWrite(VPIN_STATUS_TANK, tankText);
    lastSentTankStatus = tankText;
  }
}

// ---------------------------------------------------------------------------
// Control pass — runs every CONTROL_INTERVAL_MS
// ---------------------------------------------------------------------------
void runControlPass() {
  water::SensorState sensors;
  sensors.wellEmpty = !probeSubmerged(PIN_WELL_LOW);
  sensors.wellFull = probeSubmerged(PIN_WELL_HIGH);
  sensors.tankEmpty = !probeSubmerged(PIN_TANK_LOW);
  sensors.tankFull = probeSubmerged(PIN_TANK_HIGH);

  // A zero timeout keeps this non-blocking. The default waits up to five
  // seconds for a valid clock, which would stall this callback on every pass
  // until NTP syncs — and permanently while offline.
  struct tm timeinfo;
  const bool timeValid = getLocalTime(&timeinfo, 0);
  const int nowMinute = timeValid ? (timeinfo.tm_hour * 60 + timeinfo.tm_min) : 0;

  outputs = water::decideOutputs(sensors, manualOverrides, morningWindow,
                                 eveningWindow, timeValid, nowMinute, outputs);

  digitalWrite(PIN_RELAY_SUBMERSIBLE, outputs.submersiblePump ? RELAY_ON : RELAY_OFF);
  digitalWrite(PIN_RELAY_IRRIGATION, outputs.irrigationPump ? RELAY_ON : RELAY_OFF);
  digitalWrite(PIN_RELAY_SOLENOID, outputs.solenoid ? RELAY_ON : RELAY_OFF);

  publishTelemetry(sensors);
}

// ---------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------
void maintainWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  const unsigned long now = millis();
  if (now - lastWifiAttemptMs < WIFI_RETRY_INTERVAL_MS) {
    return;
  }
  lastWifiAttemptMs = now;

  Serial.println("[SYSTEM] Wi-Fi down, retrying...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ---------------------------------------------------------------------------
// Start-up helpers
// ---------------------------------------------------------------------------
void initialiseRelays() {
  const int relayPins[] = {PIN_RELAY_SUBMERSIBLE, PIN_RELAY_IRRIGATION,
                           PIN_RELAY_SOLENOID,    PIN_RELAY_LIGHT_INNER,
                           PIN_RELAY_LIGHT_OUTER, PIN_RELAY_SPARE_1,
                           PIN_RELAY_SPARE_2};
  for (int pin : relayPins) {
    // Latch the OFF level before enabling the driver, so the coils do not click
    // on for a moment as the pin switches to output.
    digitalWrite(pin, RELAY_OFF);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, RELAY_OFF);
  }
}

void initialiseProbes() {
  const int probePins[] = {PIN_WELL_LOW, PIN_WELL_HIGH, PIN_TANK_LOW, PIN_TANK_HIGH};
  for (int pin : probePins) {
    pinMode(pin, INPUT_PULLUP);
  }
}

void initialiseWatchdog() {
  Serial.println("[SYSTEM] Configuring watchdog timer...");
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t wdtConfig = {};
  wdtConfig.timeout_ms = WDT_TIMEOUT_S * 1000;
  wdtConfig.idle_core_mask = (1 << portNUM_PROCESSORS) - 1;
  wdtConfig.trigger_panic = true;

  // Core 3.x already initialises the task watchdog during start-up, so a plain
  // init() call fails with ESP_ERR_INVALID_STATE and silently leaves the
  // default timeout in place. Reconfigure it instead when that happens.
  esp_err_t err = esp_task_wdt_init(&wdtConfig);
  if (err == ESP_ERR_INVALID_STATE) {
    err = esp_task_wdt_reconfigure(&wdtConfig);
  }
  if (err != ESP_OK) {
    Serial.printf("[SYSTEM] Watchdog configuration failed: %s\n", esp_err_to_name(err));
  }
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif

  // Watch the Arduino loop task. Already-subscribed is not an error worth
  // stopping for.
  esp_task_wdt_add(NULL);
}

void connectWifiInitial() {
  Serial.println("[SYSTEM] Joining Wi-Fi network...");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    esp_task_wdt_reset();  // the watchdog is armed by now — keep feeding it
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[SYSTEM] Wi-Fi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    // Not fatal: the pump logic runs perfectly well offline, and maintainWifi()
    // keeps retrying in the background.
    Serial.println("[SYSTEM] Wi-Fi timed out. Running in offline mode.");
  }
  lastWifiAttemptMs = millis();
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);  // let the USB serial monitor attach before the first messages

  initialiseRelays();
  initialiseProbes();
  initialiseWatchdog();
  connectWifiInitial();

  Blynk.config(BLYNK_AUTH_TOKEN, BLYNK_SERVER, BLYNK_PORT);
  if (WiFi.status() == WL_CONNECTED) {
    // Bounded on purpose: the default blocks for about 30 s, which is twice the
    // watchdog timeout, so an unreachable cloud would reboot the board in a loop.
    Blynk.connect(BLYNK_CONNECT_TIMEOUT_MS);
    esp_task_wdt_reset();
  }

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "id.pool.ntp.org",
             "pool.ntp.org", "time.google.com");

  timer.setInterval(CONTROL_INTERVAL_MS, runControlPass);

  // Settle the outputs now rather than leaving them until the first tick.
  runControlPass();
}

void loop() {
  esp_task_wdt_reset();

  maintainWifi();
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();  // reconnects to the cloud by itself when the link returns
  }
  timer.run();
}
