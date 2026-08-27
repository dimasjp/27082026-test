// Hardware map and tuning constants for the Rumah Pompa controller.

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Relay polarity
//
// The relay board is ACTIVE LOW: pulling the pin LOW energises the coil and
// switches the load ON; HIGH releases it. Every relay write goes through these
// two constants so the polarity is stated in exactly one place.
// ---------------------------------------------------------------------------
constexpr uint8_t RELAY_ON = LOW;
constexpr uint8_t RELAY_OFF = HIGH;

// ---------------------------------------------------------------------------
// Relay outputs
//
// NOTE: GPIO12 is an ESP32 strapping pin (MTDI). It is sampled at boot and must
// read LOW, but an idle active-low relay holds it HIGH, which can stop the
// board from booting. See README.md — moving this relay to a free GPIO such as
// 19 or 21 is recommended.
// ---------------------------------------------------------------------------
constexpr int PIN_RELAY_SUBMERSIBLE = 13;  // Relay 1: well pump (fills the tank)
constexpr int PIN_RELAY_IRRIGATION = 12;   // Relay 2: garden watering pump
constexpr int PIN_RELAY_SOLENOID = 14;     // Relay 3: irrigation solenoid valve
constexpr int PIN_RELAY_LIGHT_INNER = 27;  // Relay 4: light inside the garden
constexpr int PIN_RELAY_LIGHT_OUTER = 26;  // Relay 5: light outside the beds
constexpr int PIN_RELAY_SPARE_1 = 23;      // Relay 6: spare switch 1 (MISO)
constexpr int PIN_RELAY_SPARE_2 = 18;      // Relay 7: spare switch 2 (SCK)

// ---------------------------------------------------------------------------
// Level probes (three-wire electrodes, wired to ground when submerged)
//
// Each pin uses the internal pull-up, so: LOW = electrode touching water,
// HIGH = electrode dry.
// ---------------------------------------------------------------------------
constexpr int PIN_WELL_LOW = 32;   // lower electrode, well
constexpr int PIN_WELL_HIGH = 4;   // upper electrode, well
constexpr int PIN_TANK_LOW = 33;   // lower electrode, storage tank
constexpr int PIN_TANK_HIGH = 25;  // upper electrode, storage tank

// ---------------------------------------------------------------------------
// Blynk virtual pins
//
// These stay as macros rather than constants: BLYNK_WRITE() pastes its argument
// into a function name, so it needs the literal `V6` token.
// ---------------------------------------------------------------------------
#define VPIN_MONITOR_SUBMERSIBLE V1
#define VPIN_MONITOR_IRRIGATION V2
#define VPIN_MONITOR_SOLENOID V3
#define VPIN_STATUS_WELL V4
#define VPIN_STATUS_TANK V5
#define VPIN_CONTROL_SUBMERSIBLE V6
#define VPIN_CONTROL_IRRIGATION V7
#define VPIN_CONTROL_LIGHT_INNER V8
#define VPIN_CONTROL_LIGHT_OUTER V9
#define VPIN_SCHEDULE_MORNING V10
#define VPIN_SCHEDULE_EVENING V11
#define VPIN_CONTROL_SPARE_1 V12
#define VPIN_CONTROL_SPARE_2 V13

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
constexpr unsigned long CONTROL_INTERVAL_MS = 2000;        // sensor + logic pass
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 30000;    // reconnect attempt spacing
constexpr unsigned long TELEMETRY_REFRESH_MS = 30000;      // forced full app resync
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000;   // initial Wi-Fi wait
constexpr unsigned long BLYNK_CONNECT_TIMEOUT_MS = 5000;   // initial Blynk wait
constexpr int WDT_TIMEOUT_S = 15;                          // watchdog tolerance

// Clock. 25200 s = GMT+7 (WIB). Indonesia does not observe DST, so the
// daylight offset stays at 0.
constexpr long GMT_OFFSET_SEC = 7 * 3600;
constexpr int DAYLIGHT_OFFSET_SEC = 0;

// Default watering schedule, used until the app sends its own via the
// TimeInput widgets.
constexpr int DEFAULT_MORNING_HOUR = 7;
constexpr int DEFAULT_MORNING_MINUTE = 0;
constexpr int DEFAULT_MORNING_DURATION_MIN = 15;
constexpr int DEFAULT_EVENING_HOUR = 16;
constexpr int DEFAULT_EVENING_MINUTE = 30;
constexpr int DEFAULT_EVENING_DURATION_MIN = 15;

#endif  // CONFIG_H
