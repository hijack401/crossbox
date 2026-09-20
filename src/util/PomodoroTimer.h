#pragma once

#include <cstdint>

class PomodoroTimer {
 public:
  static constexpr uint32_t MAX_DURATION_SECONDS = 180 * 60;

  bool start(const uint32_t nowMs, const uint32_t durationSeconds) {
    if (durationSeconds == 0 || durationSeconds > MAX_DURATION_SECONDS) {
      return false;
    }
    duration = durationSeconds;
    remainingMs = durationSeconds * 1000;
    startedAt = nowMs;
    state = State::Running;
    return true;
  }

  // Returns true only when this call completes a running session.
  bool tick(const uint32_t nowMs) {
    if (state != State::Running || nowMs - startedAt < remainingMs) {
      return false;
    }
    remainingMs = 0;
    state = State::Complete;
    return true;
  }

  // Also reports completion if the pause arrives at or after the deadline.
  bool pause(const uint32_t nowMs) {
    if (tick(nowMs)) {
      return true;
    }
    if (state == State::Running) {
      remainingMs -= nowMs - startedAt;
      state = State::Paused;
    }
    return false;
  }

  void resume(const uint32_t nowMs) {
    if (state == State::Paused) {
      startedAt = nowMs;
      state = State::Running;
    }
  }

  void reset() {
    duration = 0;
    remainingMs = 0;
    startedAt = 0;
    state = State::Idle;
  }

  uint32_t remainingSeconds(const uint32_t nowMs) const {
    uint32_t remaining = remainingMs;
    if (state == State::Running) {
      // Unsigned subtraction handles a millis() wrap during the session.
      const uint32_t elapsed = nowMs - startedAt;
      remaining = elapsed >= remaining ? 0 : remaining - elapsed;
    }
    return (remaining + 999) / 1000;
  }

  uint32_t totalSeconds() const { return duration; }
  bool isRunning() const { return state == State::Running; }
  bool isPaused() const { return state == State::Paused; }
  bool isComplete() const { return state == State::Complete; }

 private:
  enum class State : uint8_t { Idle, Running, Paused, Complete };

  uint32_t duration = 0;
  uint32_t remainingMs = 0;
  uint32_t startedAt = 0;
  State state = State::Idle;
};
