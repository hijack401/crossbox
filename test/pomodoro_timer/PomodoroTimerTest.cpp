#include <gtest/gtest.h>

#include <cstdint>

#include "util/PomodoroTimer.h"

TEST(PomodoroTimer, StartsIdleWithoutACompletionEvent) {
  PomodoroTimer timer;
  EXPECT_FALSE(timer.isRunning());
  EXPECT_FALSE(timer.isPaused());
  EXPECT_FALSE(timer.isComplete());
  EXPECT_EQ(timer.totalSeconds(), 0u);
  EXPECT_EQ(timer.remainingSeconds(1000), 0u);
  EXPECT_FALSE(timer.tick(1000));
}

TEST(PomodoroTimer, SupportsAllPresetsAndCustomDurationLimits) {
  PomodoroTimer timer;
  for (const uint32_t minutes : {1u, 25u, 50u, 90u, 180u}) {
    ASSERT_TRUE(timer.start(12345, minutes * 60));
    EXPECT_TRUE(timer.isRunning());
    EXPECT_EQ(timer.totalSeconds(), minutes * 60);
    EXPECT_EQ(timer.remainingSeconds(12345), minutes * 60);
    EXPECT_TRUE(timer.tick(12345 + minutes * 60000));
    EXPECT_TRUE(timer.isComplete());
  }
}

TEST(PomodoroTimer, RejectsInvalidDurationsWithoutDisturbingSession) {
  PomodoroTimer timer;
  ASSERT_TRUE(timer.start(1000, 1500));
  EXPECT_FALSE(timer.start(2000, 0));
  EXPECT_FALSE(timer.start(2000, PomodoroTimer::MAX_DURATION_SECONDS + 1));
  EXPECT_FALSE(timer.start(2000, UINT32_MAX));
  EXPECT_EQ(timer.totalSeconds(), 1500u);
  EXPECT_EQ(timer.remainingSeconds(2000), 1499u);
}

TEST(PomodoroTimer, DisplaysLastSecondUntilTheActualDeadline) {
  PomodoroTimer timer;
  ASSERT_TRUE(timer.start(1000, 60));
  EXPECT_EQ(timer.remainingSeconds(1000), 60u);
  EXPECT_EQ(timer.remainingSeconds(1001), 60u);
  EXPECT_EQ(timer.remainingSeconds(1999), 60u);
  EXPECT_EQ(timer.remainingSeconds(2000), 59u);
  EXPECT_EQ(timer.remainingSeconds(60999), 1u);
  EXPECT_FALSE(timer.tick(60999));
  EXPECT_EQ(timer.remainingSeconds(61000), 0u);
  EXPECT_TRUE(timer.tick(61000));
}

TEST(PomodoroTimer, CompletionIsReportedExactlyOnceAndCannotResume) {
  PomodoroTimer timer;
  ASSERT_TRUE(timer.start(0, 60));
  EXPECT_TRUE(timer.tick(60000));
  EXPECT_FALSE(timer.isRunning());
  EXPECT_TRUE(timer.isComplete());
  EXPECT_FALSE(timer.tick(60000));
  EXPECT_FALSE(timer.tick(62000));
  EXPECT_FALSE(timer.pause(65000));
  timer.resume(70000);
  EXPECT_TRUE(timer.isComplete());
  EXPECT_FALSE(timer.isRunning());
  EXPECT_EQ(timer.remainingSeconds(70000), 0u);
}

TEST(PomodoroTimer, SlowRefreshesDoNotAccumulateClockDrift) {
  PomodoroTimer timer;
  ASSERT_TRUE(timer.start(123, 1500));
  EXPECT_FALSE(timer.tick(2731));
  EXPECT_EQ(timer.remainingSeconds(2731), 1498u);
  EXPECT_FALSE(timer.tick(1499999));
  EXPECT_EQ(timer.remainingSeconds(1499999), 1u);
  EXPECT_TRUE(timer.tick(1506123));
  EXPECT_EQ(timer.remainingSeconds(1506123), 0u);
  EXPECT_FALSE(timer.tick(1509000));
}

TEST(PomodoroTimer, PausePreservesFractionalSecondsAndIgnoresPausedTime) {
  PomodoroTimer timer;
  ASSERT_TRUE(timer.start(1000, 60));
  EXPECT_FALSE(timer.pause(1250));
  EXPECT_TRUE(timer.isPaused());
  EXPECT_EQ(timer.remainingSeconds(100000), 60u);
  EXPECT_FALSE(timer.tick(100000));
  EXPECT_FALSE(timer.pause(100000));
  timer.resume(200000);
  EXPECT_TRUE(timer.isRunning());
  EXPECT_EQ(timer.remainingSeconds(200749), 60u);
  EXPECT_EQ(timer.remainingSeconds(200750), 59u);
  EXPECT_FALSE(timer.pause(200750));
  timer.resume(300000);
  EXPECT_FALSE(timer.tick(358999));
  EXPECT_EQ(timer.remainingSeconds(358999), 1u);
  EXPECT_TRUE(timer.tick(359000));
}

TEST(PomodoroTimer, ResumeOnRunningTimerDoesNotExtendDeadline) {
  PomodoroTimer timer;
  ASSERT_TRUE(timer.start(1000, 60));
  timer.resume(50000);
  EXPECT_TRUE(timer.tick(61000));
}

TEST(PomodoroTimer, PauseAtDeadlineCompletesInsteadOfFreezingZero) {
  PomodoroTimer timer;
  ASSERT_TRUE(timer.start(0, 60));
  EXPECT_TRUE(timer.pause(60000));
  EXPECT_TRUE(timer.isComplete());
  EXPECT_FALSE(timer.isPaused());
  EXPECT_FALSE(timer.tick(60001));
}

TEST(PomodoroTimer, CountsAcrossMillisRollover) {
  PomodoroTimer timer;
  constexpr uint32_t start = UINT32_MAX - 500;
  ASSERT_TRUE(timer.start(start, 60));
  EXPECT_EQ(timer.remainingSeconds(499), 59u);
  EXPECT_FALSE(timer.tick(59498));
  EXPECT_EQ(timer.remainingSeconds(59498), 1u);
  EXPECT_TRUE(timer.tick(59499));
  EXPECT_FALSE(timer.tick(59500));
}

TEST(PomodoroTimer, CanPauseAcrossMillisRollover) {
  PomodoroTimer timer;
  ASSERT_TRUE(timer.start(UINT32_MAX - 500, 60));
  EXPECT_FALSE(timer.pause(249));
  EXPECT_TRUE(timer.isPaused());
  timer.resume(UINT32_MAX - 500);
  EXPECT_FALSE(timer.tick(58748));
  EXPECT_TRUE(timer.tick(58749));
}

TEST(PomodoroTimer, ResetCancelsSessionAndRestartDiscardsPreviousProgress) {
  PomodoroTimer timer;
  ASSERT_TRUE(timer.start(0, 1500));
  EXPECT_FALSE(timer.pause(10000));
  timer.reset();
  EXPECT_FALSE(timer.isRunning());
  EXPECT_FALSE(timer.isPaused());
  EXPECT_FALSE(timer.isComplete());
  EXPECT_EQ(timer.totalSeconds(), 0u);
  EXPECT_EQ(timer.remainingSeconds(1500000), 0u);
  EXPECT_FALSE(timer.tick(1500000));
  ASSERT_TRUE(timer.start(2000000, 3000));
  EXPECT_EQ(timer.remainingSeconds(2000000), 3000u);
  EXPECT_TRUE(timer.tick(5000000));
  ASSERT_TRUE(timer.start(6000000, 5400));
  EXPECT_FALSE(timer.isComplete());
  EXPECT_EQ(timer.remainingSeconds(6000000), 5400u);
}
