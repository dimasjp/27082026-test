# Rumah Pompa — ESP32 Well, Tank & Irrigation Controller

Firmware for an ESP32 that manages a household water system: it refills a
storage tank from a well, waters the garden on a schedule, and switches garden
lighting — all controllable from a phone through Blynk, and all still working
when the internet is down.

## What it does

**Tank refill.** A submersible pump moves water from the well into the storage
tank. Filling starts when the tank drops below its lower electrode *and* the
well has recovered to its upper electrode, and continues until the tank reaches
its upper electrode. Between those two points the pump holds its state, which is
what stops it short-cycling as the water surface moves around a probe.

**Dry-run protection.** If the well's lower electrode goes dry, the submersible
pump is cut immediately and cannot be started — not even by the manual switch in
the app. Running a submersible pump without water destroys it in minutes, so this
one rule outranks everything else.

**Scheduled watering.** Two daily windows (07:00 and 16:30 by default, 15 minutes
each) open the solenoid valve and run the irrigation pump together. Both times
and durations are editable from the app. Watering is cancelled while the tank is
empty.

**Offline operation.** Wi-Fi and the Blynk cloud are optional. If either is
unavailable the controller keeps running pump and tank logic locally and retries
the connection in the background. Only the scheduled watering needs the network,
because it needs NTP for the clock.

## Layout

```
RumahPompa/
  RumahPompa.ino       Hardware + connectivity: pins, Wi-Fi, Blynk, watchdog
  water_logic.h/.cpp   Pure decision logic — no Arduino dependency
  config.h             Pin map, virtual pins, timings, defaults
  secrets.example.h    Credential template (copy to secrets.h)
test/
  test_water_logic.cpp Host-side tests for the decision logic
  run_tests.sh         Build and run them with a plain g++
platformio.ini         PlatformIO build; Arduino IDE works too
```

The decision logic is deliberately split out of the sketch. It touches no
hardware, so it compiles and runs on a normal computer, which is how the
scheduling and pump-latching rules are tested without an ESP32 on the bench:

```sh
./test/run_tests.sh
```

## Getting started

1. **Credentials.** Copy the template and fill it in:

   ```sh
   cp RumahPompa/secrets.example.h RumahPompa/secrets.h
   ```

   `secrets.h` is git-ignored. The sketch refuses to compile without it.

2. **Build.** Either open `RumahPompa/RumahPompa.ino` in the Arduino IDE with
   the ESP32 board package installed and the Blynk library added through the
   Library Manager, or run `pio run -t upload` from the repository root.

3. **Blynk app.** Create the datastreams listed under *Virtual pins* below and
   place the matching widgets. The two schedule pins need TimeInput widgets; set
   them to accept a start *and* stop time if you want to control the watering
   duration from the phone.

## Hardware

### Relay outputs (active low)

| Relay | GPIO | Load |
|-------|------|------|
| 1 | 13 | Submersible well pump |
| 2 | 12 | Garden irrigation pump |
| 3 | 14 | Irrigation solenoid valve |
| 4 | 27 | Light inside the garden |
| 5 | 26 | Light outside the beds |
| 6 | 23 | Spare switch 1 |
| 7 | 18 | Spare switch 2 |

The relay board is active low: the pin is driven LOW to switch a load on. This
is expressed once, as `RELAY_ON` / `RELAY_OFF` in `config.h`, rather than
repeated at every `digitalWrite`.

> **⚠️ GPIO12 is a strapping pin.** On the ESP32, GPIO12 (MTDI) is sampled at
> reset and must read LOW; it selects the internal flash voltage. An idle
> active-low relay input holds it HIGH, which can leave the board unable to
> boot, or booting into a bad flash voltage. The pin map above reproduces the
> original wiring, but **moving relay 2 to a free GPIO such as 19 or 21 is
> recommended.** Change `PIN_RELAY_IRRIGATION` in `config.h` if you rewire it.

### Level probes

Three-wire electrode probes, one common line to ground. Each sense pin uses the
ESP32's internal pull-up, so a pin reads LOW when its electrode is submerged and
HIGH when it is dry.

| Probe | GPIO | Meaning |
|-------|------|---------|
| Well, lower  | 32 | below this = well dry, pump blocked |
| Well, upper  | 4  | at this = well recovered |
| Tank, lower  | 33 | below this = tank empty, start refill |
| Tank, upper  | 25 | at this = tank full, stop refill |

### Virtual pins

| Pin | Direction | Purpose |
|-----|-----------|---------|
| V1  | → app | Submersible pump state |
| V2  | → app | Irrigation pump state |
| V3  | → app | Solenoid valve state |
| V4  | → app | Well status text |
| V5  | → app | Tank status text |
| V6  | ← app | Manual submersible pump |
| V7  | ← app | Manual irrigation |
| V8  | ← app | Inner garden light |
| V9  | ← app | Outer light |
| V10 | ← app | Morning watering schedule (TimeInput) |
| V11 | ← app | Evening watering schedule (TimeInput) |
| V12 | ← app | Spare switch 1 |
| V13 | ← app | Spare switch 2 |

## Notes and known limitations

**Manual watering overrides the empty-tank cutoff.** Turning on manual
irrigation (V7) runs the pump even when the tank reads empty, so the garden can
be watered while the tank is refilling. This is deliberate and matches the
original behaviour, but note that it is *not* symmetrical with the submersible
pump, whose dry-run protection cannot be overridden. If your irrigation pump is
also damaged by running dry, remove the manual branch in
`water_logic.cpp::decideOutputs`.

**The probes are not debounced.** Readings are taken straight from the pins
every two seconds. A disturbed water surface can therefore chatter a relay
around a probe's threshold. The latching refill cycle avoids this for the
submersible pump, but the tank-empty cutoff has no such guard. If you see relay
chatter in practice, require N consecutive identical readings before acting on a
level change.

**The watchdog reboots the board after 15 seconds of no progress.** Anything
added to `loop()` or to the control pass must stay well under that.
