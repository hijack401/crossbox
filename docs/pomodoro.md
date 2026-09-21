# Pomodoro timer

Open **Home → Apps → Pomodoro**. Choose 25, 50, or 90 minutes,
or use **Custom duration** to select 1–180 minutes. Press **Focus** to start.
Custom duration shows a large minute readout, a wide drag slider, and one-minute
decrement/increment buttons. **Set duration** applies the draft; Back discards it.
Physical Left/Right buttons change one minute, and side buttons change five.
The minimal screen shows minutes and seconds remaining in bold, angular Chakra Petch digits,
with a thin remaining-time bar and Pause / Resume and End session controls.
The bar starts full and empties as time passes; pausing also holds the bar.
Back (or swiping right from the left edge) returns from the timer to Apps;
Home returns directly to the main menu. During a running or paused session,
both ask before ending it. Button-only devices keep their physical button hints.
Pomodoro buttons and the duration slider have square corners in every theme.

At zero, a clean refresh reveals a square checkmark, **Session complete**, and
the number of minutes focused. **Done** is the primary action; **Focus again**
starts another session of the same duration. X4 Pro's SDK profile has no audio output
or buzzer; completion is visual. Normal automatic sleep resumes after expiry.

Running and paused sessions keep the reader awake. The timer is intended to
stay open: the control-center overlay is disabled during a session so it
cannot suspend the timer loop. Holding Power to sleep, restarting, or powering
off ends the session. There is no background alarm or saved timer state.

## Implementation

- `src/util/PomodoroTimer.h` uses monotonic milliseconds, handles counter rollover,
  preserves fractional seconds when paused, and derives the countdown from elapsed
  time. Delayed screen refreshes do not lengthen the session.
- The activity uses the existing shared render task. Digits are drawn directly
  into the framebuffer, with no separate image buffer or timer task. The numeric
  bitmap font occupies 13,127 bytes of flash data. Regenerate it from the bundled
  Chakra Petch Bold source with `python scripts/generate_countdown_font.py`
  (requires `freetype-py`). The source font includes its OFL license and provenance.
- Countdown redraws are requested when the displayed second changes. Fast refresh
  is used between clean refreshes once per minute and at state transitions.
  Actual refresh timing and ghosting depend on the physical panel.
- The remaining-time bar shares these redraws and the completion emblem uses
  drawing primitives; neither adds a timer, image buffer, or heap allocation.
- The timer makes no SD writes. Custom duration uses the same activity and numeric
  font; its draft is stored separately until applied, with no extra picker allocation.

## Verification

Run the host model tests with the repository's CMake test setup:

```sh
cmake -S test -B build/host-tests
cmake --build build/host-tests --target PomodoroTimerTest
ctest --test-dir build/host-tests -R '^PomodoroTimer\.' --output-on-failure
```

Build the X4 Pro firmware with `pio run -e x4pro`. A configured desktop simulator
can be rebuilt with `pio run -e simulator_x4_pro`; its native executable must be
repackaged if launched through a local macOS app bundle.

Before flashing a release, verify:

1. All three presets and custom limits; cancelling custom selection preserves
   the previous value.
2. A one-minute session reaches completion once, pause holds its value, resume
   continues it, and Back/Home confirmation either keeps or ends it.
3. The timer stays awake past the configured sleep timeout, then ordinary sleep
   resumes after completion. Manual Power sleep still works.
4. Home and timer controls remain reachable in all four orientations and themes.
5. On hardware, check display ghosting/refresh timing and serial heap logs while
   repeatedly entering, running, pausing, and leaving the timer. The simulator
   does not reproduce panel waveforms or physical power use.
