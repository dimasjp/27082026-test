// Host-side tests for the pure water-management logic.
//
//   ./test/run_tests.sh
//
// These compile with a plain g++ — no ESP32 toolchain needed — because
// water_logic.cpp has no Arduino dependency.

#include "water_logic.h"

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* what) {
  ++checks;
  if (!condition) {
    ++failures;
    std::printf("  FAIL: %s\n", what);
  }
}

constexpr int at(int hour, int minute) { return hour * 60 + minute; }

// --- Window arithmetic -----------------------------------------------------

void testWindowBasics() {
  std::printf("window: ordinary daytime slot\n");
  const water::WateringWindow morning = {at(7, 0), 15};

  check(!water::isWithinWindow(morning, at(6, 59)), "closed one minute before start");
  check(water::isWithinWindow(morning, at(7, 0)), "open at the start minute");
  check(water::isWithinWindow(morning, at(7, 14)), "open at the last minute");
  check(!water::isWithinWindow(morning, at(7, 15)), "closed at start + duration");
  check(!water::isWithinWindow(morning, at(16, 30)), "closed in the evening");
}

void testWindowWrapsMidnight() {
  // The original code computed `start + duration` and compared it against a
  // clock that never exceeds 23:59, so this window silently ended at midnight.
  std::printf("window: slot running past midnight\n");
  const water::WateringWindow lateNight = {at(23, 55), 15};

  check(!water::isWithinWindow(lateNight, at(23, 54)), "closed before start");
  check(water::isWithinWindow(lateNight, at(23, 55)), "open at the start minute");
  check(water::isWithinWindow(lateNight, at(23, 59)), "open just before midnight");
  check(water::isWithinWindow(lateNight, at(0, 0)), "still open after midnight");
  check(water::isWithinWindow(lateNight, at(0, 9)), "open at the last minute");
  check(!water::isWithinWindow(lateNight, at(0, 10)), "closed at start + duration");
  check(!water::isWithinWindow(lateNight, at(12, 0)), "closed at midday");
}

void testWindowDegenerateDurations() {
  std::printf("window: degenerate durations\n");
  check(!water::isWithinWindow({at(7, 0), 0}, at(7, 0)), "zero duration never opens");
  check(!water::isWithinWindow({at(7, 0), -5}, at(7, 0)), "negative duration never opens");
  check(water::isWithinWindow({at(7, 0), 1440}, at(3, 0)), "full-day duration always open");
}

void testWindowDuration() {
  std::printf("windowDuration: start/stop arithmetic\n");
  check(water::windowDuration(at(7, 0), at(7, 20)) == 20, "same-day span");
  check(water::windowDuration(at(23, 50), at(0, 10)) == 20, "span across midnight");
  check(water::windowDuration(at(7, 0), at(7, 0)) == 0, "empty span reports zero");
}

// --- Submersible pump ------------------------------------------------------

const water::WateringWindow kMorning = {at(7, 0), 15};
const water::WateringWindow kEvening = {at(16, 30), 15};
const water::OutputState kAllOff = {false, false, false};

water::OutputState decide(const water::SensorState& sensors,
                          const water::ManualOverrides& manual, int nowMinute,
                          const water::OutputState& previous, bool timeValid = true) {
  return water::decideOutputs(sensors, manual, kMorning, kEvening, timeValid,
                              nowMinute, previous);
}

void testDryRunProtection() {
  std::printf("submersible: dry-run protection\n");
  const water::SensorState wellDry = {true, false, true, false};

  check(!decide(wellDry, {false, false}, at(12, 0), kAllOff).submersiblePump,
        "pump stays off when the well is dry");
  check(!decide(wellDry, {true, false}, at(12, 0), kAllOff).submersiblePump,
        "manual override cannot run the pump dry");

  const water::OutputState running = {true, false, false};
  check(!decide(wellDry, {true, false}, at(12, 0), running).submersiblePump,
        "a running pump is cut off when the well empties");
}

void testFillCycleLatches() {
  std::printf("submersible: latching fill cycle\n");
  // Tank low, well recovered -> start filling.
  const water::SensorState tankLowWellFull = {false, true, true, false};
  water::OutputState state = decide(tankLowWellFull, {false, false}, at(12, 0), kAllOff);
  check(state.submersiblePump, "fill starts when the tank is low and the well is full");

  // Water rises past the lower electrode but has not reached the top: neither
  // condition fires, so the pump must hold its state instead of stopping.
  const water::SensorState midLevel = {false, true, false, false};
  state = decide(midLevel, {false, false}, at(12, 0), state);
  check(state.submersiblePump, "fill continues between the two electrodes");

  // Tank full -> stop.
  const water::SensorState tankFull = {false, true, false, true};
  state = decide(tankFull, {false, false}, at(12, 0), state);
  check(!state.submersiblePump, "fill stops when the tank is full");

  // Still full on the next pass: must not restart.
  state = decide(tankFull, {false, false}, at(12, 0), state);
  check(!state.submersiblePump, "pump stays off while the tank remains full");

  // Level drops below the top electrode again: must not restart until the
  // tank reaches its lower electrode.
  state = decide(midLevel, {false, false}, at(12, 0), state);
  check(!state.submersiblePump, "pump does not short-cycle below the top electrode");
}

void testManualSubmersible() {
  std::printf("submersible: manual override\n");
  const water::SensorState tankFullWellFull = {false, true, false, true};
  check(decide(tankFullWellFull, {true, false}, at(12, 0), kAllOff).submersiblePump,
        "manual run works with a full tank when the well has water");
}

// --- Irrigation ------------------------------------------------------------

void testScheduledWatering() {
  std::printf("irrigation: scheduled windows\n");
  const water::SensorState tankOk = {false, true, false, false};

  water::OutputState state = decide(tankOk, {false, false}, at(7, 5), kAllOff);
  check(state.irrigationPump && state.solenoid, "morning window opens pump and valve");

  state = decide(tankOk, {false, false}, at(16, 35), kAllOff);
  check(state.irrigationPump && state.solenoid, "evening window opens pump and valve");

  state = decide(tankOk, {false, false}, at(12, 0), state);
  check(!state.irrigationPump && !state.solenoid, "both close outside the windows");
}

void testWateringNeedsValidClock() {
  std::printf("irrigation: schedule ignored without a synced clock\n");
  const water::SensorState tankOk = {false, true, false, false};
  const water::OutputState state =
      decide(tankOk, {false, false}, at(7, 5), kAllOff, /*timeValid=*/false);
  check(!state.irrigationPump && !state.solenoid,
        "no scheduled watering while the clock is unset");
}

void testTankProtection() {
  std::printf("irrigation: empty-tank protection\n");
  const water::SensorState tankEmpty = {false, true, true, false};

  water::OutputState state = decide(tankEmpty, {false, false}, at(7, 5), kAllOff);
  check(!state.irrigationPump && !state.solenoid,
        "an empty tank cancels scheduled watering");

  // Documented behaviour: the manual override deliberately outranks the tank
  // check so the garden can still be watered while the tank refills.
  state = decide(tankEmpty, {false, true}, at(12, 0), kAllOff);
  check(state.irrigationPump && state.solenoid,
        "manual watering overrides the empty-tank cutoff");
}

void testPumpAndValveMoveTogether() {
  std::printf("irrigation: pump and valve stay in step\n");
  const water::SensorState tankOk = {false, true, false, false};
  const int probes[] = {at(0, 0), at(7, 0), at(7, 14), at(7, 15), at(16, 30), at(23, 59)};
  for (int minute : probes) {
    const water::OutputState state = decide(tankOk, {false, false}, minute, kAllOff);
    check(state.irrigationPump == state.solenoid,
          "pump and valve agree at every time of day");
  }
}

}  // namespace

int main() {
  testWindowBasics();
  testWindowWrapsMidnight();
  testWindowDegenerateDurations();
  testWindowDuration();
  testDryRunProtection();
  testFillCycleLatches();
  testManualSubmersible();
  testScheduledWatering();
  testWateringNeedsValidClock();
  testTankProtection();
  testPumpAndValveMoveTogether();

  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
