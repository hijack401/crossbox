#pragma once

#include <cstdint>

class CasinoRefreshPolicy {
 public:
  static constexpr uint8_t FAST_UPDATES_BEFORE_CLEAN = 40;

  // Only round/turn boundaries and returning to the lobby may interrupt with a clean.
  void allowCleaning() {
    if (fastUpdates >= FAST_UPDATES_BEFORE_CLEAN) cleanPending = true;
  }

  bool nextFrameNeedsCleaning() {
    if (cleanPending) {
      cleanPending = false;
      fastUpdates = 0;
      return true;
    }
    // Saturate so long runs of dice selection, focus moves, or bet entry cannot wrap.
    if (fastUpdates < FAST_UPDATES_BEFORE_CLEAN) ++fastUpdates;
    return false;
  }

 private:
  uint8_t fastUpdates = 0;
  bool cleanPending = true;  // Clear the previous activity once on entry.
};
