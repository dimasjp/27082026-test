#include "water_logic.h"

namespace water {

bool isWithinWindow(const WateringWindow& window, int nowMinute) {
  if (window.durationMinutes <= 0) {
    return false;
  }
  if (window.durationMinutes >= MINUTES_PER_DAY) {
    return true;
  }

  const int start = ((window.startMinute % MINUTES_PER_DAY) + MINUTES_PER_DAY) % MINUTES_PER_DAY;
  const int end = start + window.durationMinutes;

  if (end <= MINUTES_PER_DAY) {
    return nowMinute >= start && nowMinute < end;
  }
  // The window runs past midnight, so it is the union of two ranges.
  return nowMinute >= start || nowMinute < (end - MINUTES_PER_DAY);
}

int windowDuration(int startMinute, int stopMinute) {
  const int delta = stopMinute - startMinute;
  return ((delta % MINUTES_PER_DAY) + MINUTES_PER_DAY) % MINUTES_PER_DAY;
}

OutputState decideOutputs(const SensorState& sensors,
                          const ManualOverrides& manual,
                          const WateringWindow& morning,
                          const WateringWindow& evening,
                          bool timeValid,
                          int nowMinute,
                          const OutputState& previous) {
  OutputState out = previous;

  // --- Submersible pump: refill the tank from the well -------------------
  // Dry-run protection wins over everything, including the manual override:
  // running a submersible pump without water destroys it in minutes.
  if (sensors.wellEmpty) {
    out.submersiblePump = false;
  } else if (manual.submersible) {
    out.submersiblePump = true;
  } else {
    // Latching fill cycle: start once the tank is low and the well has
    // recovered, stop only when the tank is full. Between those two events the
    // previous state carries over, which is what stops the pump short-cycling
    // around the lower electrode.
    if (sensors.tankEmpty && sensors.wellFull) {
      out.submersiblePump = true;
    }
    if (sensors.tankFull) {
      out.submersiblePump = false;
    }
  }

  // --- Irrigation pump + solenoid valve ----------------------------------
  // These two always move together: the valve must be open before the pump
  // pushes against it.
  if (sensors.tankEmpty && !manual.irrigation) {
    out.irrigationPump = false;
    out.solenoid = false;
  } else if (manual.irrigation) {
    out.irrigationPump = true;
    out.solenoid = true;
  } else {
    const bool scheduled = timeValid && (isWithinWindow(morning, nowMinute) ||
                                         isWithinWindow(evening, nowMinute));
    out.irrigationPump = scheduled;
    out.solenoid = scheduled;
  }

  return out;
}

}  // namespace water
