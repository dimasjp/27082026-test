// Pure water-management decision logic for the Rumah Pompa controller.
//
// This header deliberately contains NO Arduino dependency: it is plain C++ so
// the same code that runs on the ESP32 can be compiled and unit-tested on a
// host machine (see test/test_water_logic.cpp). All hardware I/O — reading the
// probes, driving the relays — lives in RumahPompa.ino.

#ifndef WATER_LOGIC_H
#define WATER_LOGIC_H

namespace water {

constexpr int MINUTES_PER_DAY = 24 * 60;

// Level probe readings, already converted from raw pin levels to meaning.
struct SensorState {
  bool wellEmpty;   // water is below the well's lower electrode
  bool wellFull;    // water has reached the well's upper electrode
  bool tankEmpty;   // water is below the tank's lower electrode
  bool tankFull;    // water has reached the tank's upper electrode
};

// Manual overrides coming from the Blynk app.
struct ManualOverrides {
  bool submersible;
  bool irrigation;
};

// A watering slot, expressed as minutes since midnight plus a duration.
struct WateringWindow {
  int startMinute;
  int durationMinutes;
};

// Desired relay states. Also used as the *previous* state, because the
// submersible pump latches between evaluations (fill starts when the tank runs
// low and continues until the tank is full, rather than re-deciding each pass).
struct OutputState {
  bool submersiblePump;
  bool irrigationPump;
  bool solenoid;
};

// True when `nowMinute` falls inside the window. Handles windows that run past
// midnight (e.g. start 23:55 for 15 minutes covers 23:55–00:10).
bool isWithinWindow(const WateringWindow& window, int nowMinute);

// Number of minutes from `startMinute` to `stopMinute`, wrapping over midnight.
// Returns 0 when the two are equal, which callers treat as "keep the default".
int windowDuration(int startMinute, int stopMinute);

// Decide every relay state for this pass.
//
// `timeValid` reports whether the clock has actually been set from NTP; when it
// is false the scheduled windows are ignored and only the manual overrides and
// the dry-run protection apply.
OutputState decideOutputs(const SensorState& sensors,
                          const ManualOverrides& manual,
                          const WateringWindow& morning,
                          const WateringWindow& evening,
                          bool timeValid,
                          int nowMinute,
                          const OutputState& previous);

}  // namespace water

#endif  // WATER_LOGIC_H
