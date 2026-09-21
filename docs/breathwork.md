# Breathwork

Open **Home → Apps → Breathwork**. Choose an exercise, the number of breaths,
and a count pace (or seconds per phase for Box Breathing), then press **Begin**. A three-second settling countdown gives
time to let the breath out before the first inhale. **How to** explains each
pattern, the pace settings, and how to keep the exercise comfortable.

## Presets and timing

The sequence is inhale, hold after inhaling, exhale, then rest after exhaling.
An omitted hold or rest is skipped immediately. Each full sequence counts as
one breath.

| Exercise | Counts | Breath choices | Steady durations | Default |
| --- | --- | --- | --- | --- |
| Equal Breathing | 5 in / 5 out | 6 / 12 / 30 | 1:00 / 2:00 / 5:00 | 12 breaths, 2:00 |
| Longer Exhale | 4 in / 6 out | 6 / 12 / 30 | 1:00 / 2:00 / 5:00 | 12 breaths, 2:00 |
| Box Breathing | 4 in / 4 hold / 4 out / 4 rest | 4 / 8 / 12 | 1:04 / 2:08 / 3:12 | 8 breaths, 2:08 |
| 4-7-8 Breathing | 4 in / 7 hold / 8 out | 2 / 3 / 4 | 0:38 / 0:57 / 1:16 | 4 breaths, 1:16 |

**Box Breathing** has a **Seconds per phase** control. Minus and plus adjust
all four phases together from 2 to 10 seconds in one-second steps. Selecting
Box Breathing from the exercise list always starts at 4 seconds. Choosing
6 seconds with 8 breaths takes 3:12, with 6 seconds each for inhale, hold,
exhale, and rest. The pattern and session duration update with the selection.
**Breathe again** retains the chosen seconds; they are not saved on exit.

For the other exercises, **Steady** uses one second per count. **Faster** uses 750 milliseconds per count,
preserving the same ratios and reducing each session's duration to three
quarters. The setup screen shows the resulting duration, rounded up to whole
seconds. For example, two Faster 4-7-8 breaths take 28.5 seconds and display
0:29. The settling countdown is additional to the listed session duration.
Choosing a preset restores its default breath count and timing.

These are adjustable counting guides, not targets to force. The session lengths
are app choices. The four-breath maximum for 4-7-8 keeps this preset to an initial
practice rather than automatically extending it into a long session.

## Session controls

The active screen shows the current phase, counts remaining in that phase,
breath number, remaining session time, and a session progress bar. The central
guide expands on inhale, stays expanded during a hold, contracts on exhale,
and remains contracted during a rest.

**Pause** stops the timing and prompts natural breathing. **Resume** starts a
fresh inhale, repeating only the unfinished breath; completed breaths remain
counted. Consequently, the remaining time can increase slightly when resuming.
Opening **How to** during a session also pauses it; closing the help leaves it
paused until Resume is chosen.

**End**, or Back during a session, ends immediately and shows the number of
fully completed breaths. It is always possible to stop before the target.
Normal completion finishes the final whole breath. **Breathe again** repeats
the chosen settings after another settling countdown. **Done** returns to the
exercise list. Home leaves the app and discards the session.

The settling countdown and running session prevent automatic sleep. Paused,
help, setup, list, and completed screens allow the normal inactivity timeout.
Manual Power sleep remains available. Sleep, app exit, or restart discards the
session: there is no background alarm or session restoration. Breathwork writes
no session data or settings to the SD card.

## Research and comfort

- The [NHS breathing exercise](https://www.nhs.uk/mental-health/self-help/guides-tools-and-activities/breathing-exercises-for-stress/)
  describes gentle, equal counted inhales and exhales, without forcing depth.
  It suggests practicing for at least five minutes; the app also offers shorter
  sessions for learning the controls and finding a comfortable rhythm.
- [Cambridge University Hospitals](https://www.cuh.nhs.uk/patient-information/breathing-techniques-to-ease-breathlessness/)
  gives 4-in/6-out as one longer-exhale counting pattern.
- The [VA's Box Breathing instructions](https://veteranshealthlibrary.va.gov/LivingWith/BackNeck/Caring/3,84803)
  describe four equal counted phases and adjusting their length to comfort.
- [Andrew Weil's demonstration](https://www.youtube.com/watch?v=YRPh_GaiL8s)
  describes four initial 4-7-8 cycles, nasal inhales and mouth exhales, with the
  ratio preserved when count speed changes. His [official guide](https://www.drweil.com/wp-content/uploads/2016/08/BalancedLivingAnnual_2010.pdf)
  also distinguishes the count ratio from a required duration in seconds.
- [Ashford and St Peter's NHS guidance](https://www.ashfordstpeters.nhs.uk/nervous-system-regulation)
  advises a comfortable pace without forcing depth or length, and stopping or
  taking a break if dizzy or lightheaded. The app repeats this comfort guidance
  and keeps no-hold alternatives available.

These sources inform the instructions and patterns; the app makes no treatment
claims or ranking of which exercise is most popular or best for an individual.

## Implementation and verification

The visual direction draws on [Breathwrk's guided exercise screen on Mobbin](https://mobbin.com/screens/22a34ba5-9da8-41c2-800c-d1376abc076d):
a clear central cue, an expanding breathing form, and readily available pause
controls. The monochrome contours, preset icons, and completion bloom are drawn
locally with fixed-point geometry; no third-party image assets are bundled.
Buttons retain the sharp corners of the other apps.

`src/util/BreathworkSession.h` keeps its small state inline without heap
allocation. It derives progress from elapsed monotonic milliseconds, handles
counter rollover, and skips overdue phases or cycles without accumulating
refresh delay. Pausing at or after the deadline completes the session.
The activity uses the existing checked `makeUniqueNoThrow` navigation pattern:
one allocation on entry, owned and released by ActivityManager. Its timer,
button scratch space, and focus list are members, and contour tables are
`constexpr`; there are no per-count allocations in the new timing or geometry
code and no additional framebuffer or background task.

The activity requests timed redraws once per displayed count or phase change,
using fast e-ink refreshes instead of a continuous animation. State changes use
a cleaner refresh; another clean refresh is allowed after three minutes at the
start of an inhale. Physical panel speed and ghosting still need hardware
verification, especially at the Faster pace.

Run the host engine tests:

```sh
cmake -S test -B build/host-tests
cmake --build build/host-tests --target BreathworkSessionTest
ctest --test-dir build/host-tests -R '^BreathworkSession\.' --output-on-failure
```

Build with `pio run -e simulator_x4_pro` or `pio run -e x4pro`. Repackage a local
macOS simulator app bundle after rebuilding its native executable.

1. In the simulator, open all four presets, change breath counts and pace, and
   check the displayed duration against the table and pace multiplier. For Box,
   choose 6 seconds and verify 3:12 for 8 breaths and six-second phase changes.
   Check the 2- and 10-second limits, and re-enter Box to verify its 4-second default.
2. Run a short session. Check the phase order, countdown, whole-breath completion,
   and that Equal Breathing and Longer Exhale have no hold phases.
3. Pause during a Box hold, wait, then resume. The next cue must be a fresh inhale
   with the same completed-breath count. Repeat using How to, and test End, Back,
   Home, and cancellation during the settling countdown.
4. Verify touch and logical button navigation, all orientations, and inverted
   display mode. All controls and help text should remain reachable.
5. On the reader, run longer than a short configured inactivity timeout. Running
   must stay awake; pausing must allow normal sleep. Check manual Power sleep
   and confirm that waking does not resume an old breathing session.
6. Compare the cues with a stopwatch, inspect fast-refresh legibility and
   ghosting, and monitor serial heap logs while repeatedly entering and leaving
   the app. The simulator cannot validate panel waveforms or battery use.
