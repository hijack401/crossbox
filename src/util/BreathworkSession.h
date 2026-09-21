#pragma once

#include <cstdint>

class BreathworkSession {
 public:
  enum class Phase : uint8_t { Inhale, Hold, Exhale, Rest };
  enum class State : uint8_t { Idle, Running, Paused, Complete };

  bool start(const uint32_t nowMs, const uint8_t (&counts)[4], const uint16_t cycles, const uint16_t countMs = 1000) {
    if (cycles == 0 || cycles > 60 || countMs < 500 || countMs > 2000 || counts[0] == 0 || counts[2] == 0) {
      return false;
    }
    uint16_t cycleCounts = 0;
    for (const uint8_t count : counts) {
      if (count > 30) {
        return false;
      }
      cycleCounts += count;
    }
    for (uint8_t i = 0; i < 4; ++i) {
      phaseCounts[i] = counts[i];
    }
    countDuration = countMs;
    cycleDuration = static_cast<uint32_t>(cycleCounts) * countMs;
    cycleTotal = cycles;
    duration = cycleDuration * cycles;
    anchorTime = nowMs;
    anchorElapsed = 0;
    cyclesDone = 0;
    currentPhase = Phase::Inhale;
    state = State::Running;
    return true;
  }

  // Returns true only when this call completes a running session.
  bool tick(const uint32_t nowMs) {
    if (state != State::Running) {
      return false;
    }
    const uint32_t elapsed = elapsedMs(nowMs);
    if (elapsed == duration) {
      anchorElapsed = duration;
      cyclesDone = cycleTotal;
      currentPhase = phaseCounts[3] == 0 ? Phase::Exhale : Phase::Rest;
      state = State::Complete;
      return true;
    }
    cyclesDone = elapsed / cycleDuration;
    uint32_t phaseElapsed = 0;
    currentPhase = phaseAt(elapsed, phaseElapsed);
    return false;
  }

  void pause(const uint32_t nowMs) {
    tick(nowMs);
    if (state == State::Running) {
      anchorElapsed = elapsedMs(nowMs);
      state = State::Paused;
    }
  }

  // Begin a fresh breath instead of asking the user to resume a held breath.
  void resume(const uint32_t nowMs) {
    if (state == State::Paused) {
      anchorElapsed = static_cast<uint32_t>(cyclesDone) * cycleDuration;
      anchorTime = nowMs;
      currentPhase = Phase::Inhale;
      state = State::Running;
    }
  }

  void reset() { *this = BreathworkSession{}; }

  bool isRunning() const { return state == State::Running; }
  bool isPaused() const { return state == State::Paused; }
  bool isComplete() const { return state == State::Complete; }
  Phase phase() const { return currentPhase; }
  uint16_t completedCycles() const { return cyclesDone; }
  uint16_t totalCycles() const { return cycleTotal; }
  uint32_t totalMs() const { return duration; }
  uint32_t remainingMs(const uint32_t nowMs) const { return duration - elapsedMs(nowMs); }

  uint8_t remainingCounts(const uint32_t nowMs) const {
    const uint32_t elapsed = elapsedMs(nowMs);
    if (state == State::Idle || elapsed == duration) {
      return 0;
    }
    uint32_t phaseElapsed = 0;
    const Phase active = phaseAt(elapsed, phaseElapsed);
    const uint32_t phaseDuration = static_cast<uint32_t>(phaseCounts[static_cast<uint8_t>(active)]) * countDuration;
    return (phaseDuration - phaseElapsed + countDuration - 1) / countDuration;
  }

  uint16_t phaseProgressPermille(const uint32_t nowMs) const {
    if (state == State::Idle) {
      return 0;
    }
    const uint32_t elapsed = elapsedMs(nowMs);
    if (elapsed == duration) {
      return 1000;
    }
    uint32_t phaseElapsed = 0;
    const Phase active = phaseAt(elapsed, phaseElapsed);
    const uint32_t phaseDuration = static_cast<uint32_t>(phaseCounts[static_cast<uint8_t>(active)]) * countDuration;
    return phaseElapsed * 1000 / phaseDuration;
  }

 private:
  uint32_t elapsedMs(const uint32_t nowMs) const {
    if (state != State::Running) {
      return anchorElapsed;
    }
    // Session bounds keep elapsed arithmetic below a millis() wrap.
    const uint32_t elapsed = nowMs - anchorTime;
    const uint32_t remaining = duration - anchorElapsed;
    return elapsed >= remaining ? duration : anchorElapsed + elapsed;
  }

  Phase phaseAt(const uint32_t elapsed, uint32_t& phaseElapsed) const {
    phaseElapsed = elapsed % cycleDuration;
    for (uint8_t i = 0; i < 4; ++i) {
      const uint32_t phaseDuration = static_cast<uint32_t>(phaseCounts[i]) * countDuration;
      if (phaseElapsed < phaseDuration) {
        return static_cast<Phase>(i);
      }
      phaseElapsed -= phaseDuration;
    }
    return Phase::Inhale;
  }

  uint32_t anchorTime = 0;
  uint32_t anchorElapsed = 0;
  uint32_t cycleDuration = 0;
  uint32_t duration = 0;
  uint16_t countDuration = 0;
  uint16_t cyclesDone = 0;
  uint16_t cycleTotal = 0;
  uint8_t phaseCounts[4] = {};
  Phase currentPhase = Phase::Inhale;
  State state = State::Idle;
};
