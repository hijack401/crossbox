#include <gtest/gtest.h>

#include <cstdint>

#include "util/BreathworkSession.h"

namespace {
constexpr uint8_t BOX[4] = {4, 4, 4, 4};
constexpr uint8_t FOUR_SEVEN_EIGHT[4] = {4, 7, 8, 0};
constexpr uint8_t EQUAL[4] = {5, 0, 5, 0};
using Phase = BreathworkSession::Phase;
static_assert(sizeof(BreathworkSession) <= 64, "Breathwork session must remain small and inline");
}  // namespace

TEST(BreathworkSession, StartsIdleAndIgnoresInactiveOperations) {
  BreathworkSession session;
  EXPECT_FALSE(session.isRunning());
  EXPECT_FALSE(session.isPaused());
  EXPECT_FALSE(session.isComplete());
  EXPECT_EQ(session.totalCycles(), 0u);
  EXPECT_EQ(session.completedCycles(), 0u);
  EXPECT_EQ(session.totalMs(), 0u);
  EXPECT_EQ(session.remainingMs(5000), 0u);
  EXPECT_EQ(session.remainingCounts(5000), 0u);
  EXPECT_EQ(session.phaseProgressPermille(5000), 0u);
  EXPECT_FALSE(session.tick(5000));
  session.pause(5000);
  session.resume(8000);
  EXPECT_FALSE(session.isRunning());
  EXPECT_FALSE(session.isPaused());
}

TEST(BreathworkSession, BoxPhasesChangeOnExactBoundaries) {
  BreathworkSession session;
  ASSERT_TRUE(session.start(100, BOX, 2));
  EXPECT_EQ(session.totalMs(), 32000u);
  EXPECT_EQ(session.totalCycles(), 2u);
  EXPECT_EQ(session.phase(), Phase::Inhale);
  EXPECT_EQ(session.remainingCounts(100), 4u);
  EXPECT_EQ(session.phaseProgressPermille(100), 0u);
  EXPECT_FALSE(session.tick(4099));
  EXPECT_EQ(session.phase(), Phase::Inhale);
  EXPECT_EQ(session.remainingCounts(4099), 1u);
  EXPECT_EQ(session.phaseProgressPermille(4099), 999u);
  EXPECT_FALSE(session.tick(4100));
  EXPECT_EQ(session.phase(), Phase::Hold);
  EXPECT_EQ(session.remainingCounts(4100), 4u);
  EXPECT_EQ(session.phaseProgressPermille(4100), 0u);
  EXPECT_FALSE(session.tick(8100));
  EXPECT_EQ(session.phase(), Phase::Exhale);
  EXPECT_FALSE(session.tick(12100));
  EXPECT_EQ(session.phase(), Phase::Rest);
  EXPECT_EQ(session.completedCycles(), 0u);
  EXPECT_FALSE(session.tick(16100));
  EXPECT_EQ(session.phase(), Phase::Inhale);
  EXPECT_EQ(session.completedCycles(), 1u);
}

TEST(BreathworkSession, CountsRoundUpUntilTheActualCountBoundary) {
  BreathworkSession session;
  ASSERT_TRUE(session.start(123, BOX, 1, 750));
  EXPECT_EQ(session.totalMs(), 12000u);
  EXPECT_EQ(session.remainingCounts(123), 4u);
  EXPECT_EQ(session.remainingCounts(124), 4u);
  EXPECT_EQ(session.remainingCounts(872), 4u);
  EXPECT_EQ(session.remainingCounts(873), 3u);
  EXPECT_EQ(session.phaseProgressPermille(873), 250u);
  EXPECT_EQ(session.remainingMs(873), 11250u);
}

TEST(BreathworkSession, EqualBreathingSkipsBothZeroLengthHolds) {
  BreathworkSession session;
  ASSERT_TRUE(session.start(0, EQUAL, 2));
  EXPECT_FALSE(session.tick(5000));
  EXPECT_EQ(session.phase(), Phase::Exhale);
  EXPECT_EQ(session.remainingCounts(5000), 5u);
  EXPECT_FALSE(session.tick(10000));
  EXPECT_EQ(session.phase(), Phase::Inhale);
  EXPECT_EQ(session.completedCycles(), 1u);
  EXPECT_TRUE(session.tick(20000));
  EXPECT_EQ(session.phase(), Phase::Exhale);
  EXPECT_EQ(session.completedCycles(), 2u);
}

TEST(BreathworkSession, FourSevenEightHasNoPauseAfterExhale) {
  BreathworkSession session;
  ASSERT_TRUE(session.start(0, FOUR_SEVEN_EIGHT, 4));
  EXPECT_EQ(session.totalMs(), 76000u);
  EXPECT_FALSE(session.tick(4000));
  EXPECT_EQ(session.phase(), Phase::Hold);
  EXPECT_EQ(session.remainingCounts(4000), 7u);
  EXPECT_FALSE(session.tick(11000));
  EXPECT_EQ(session.phase(), Phase::Exhale);
  EXPECT_EQ(session.remainingCounts(11000), 8u);
  EXPECT_FALSE(session.tick(19000));
  EXPECT_EQ(session.phase(), Phase::Inhale);
  EXPECT_EQ(session.completedCycles(), 1u);
  EXPECT_FALSE(session.tick(75999));
  EXPECT_EQ(session.remainingCounts(75999), 1u);
  EXPECT_TRUE(session.tick(76000));
}

TEST(BreathworkSession, SupportsRestWithoutAnInhaleHold) {
  BreathworkSession session;
  constexpr uint8_t counts[4] = {2, 0, 4, 2};
  ASSERT_TRUE(session.start(0, counts, 1));
  EXPECT_FALSE(session.tick(2000));
  EXPECT_EQ(session.phase(), Phase::Exhale);
  EXPECT_FALSE(session.tick(6000));
  EXPECT_EQ(session.phase(), Phase::Rest);
  EXPECT_EQ(session.remainingCounts(6000), 2u);
  EXPECT_TRUE(session.tick(8000));
  EXPECT_EQ(session.phase(), Phase::Rest);
}

TEST(BreathworkSession, LateRefreshSkipsWholeCyclesWithoutDrift) {
  BreathworkSession session;
  ASSERT_TRUE(session.start(123, BOX, 60));
  EXPECT_FALSE(session.tick(451746));
  EXPECT_EQ(session.completedCycles(), 28u);
  EXPECT_EQ(session.phase(), Phase::Inhale);
  EXPECT_EQ(session.remainingCounts(451746), 1u);
  EXPECT_EQ(session.remainingMs(451746), 508377u);
  EXPECT_FALSE(session.tick(452123));
  EXPECT_EQ(session.phase(), Phase::Hold);
  EXPECT_EQ(session.phaseProgressPermille(452123), 0u);
  EXPECT_FALSE(session.tick(960122));
  EXPECT_EQ(session.remainingMs(960122), 1u);
  EXPECT_TRUE(session.tick(960123));
}

TEST(BreathworkSession, VeryLateTickCompletesExactlyOnce) {
  BreathworkSession session;
  ASSERT_TRUE(session.start(0, BOX, 3));
  EXPECT_TRUE(session.tick(UINT32_MAX));
  EXPECT_TRUE(session.isComplete());
  EXPECT_FALSE(session.isRunning());
  EXPECT_EQ(session.completedCycles(), 3u);
  EXPECT_EQ(session.remainingMs(UINT32_MAX), 0u);
  EXPECT_EQ(session.remainingCounts(UINT32_MAX), 0u);
  EXPECT_EQ(session.phaseProgressPermille(UINT32_MAX), 1000u);
  EXPECT_FALSE(session.tick(0));
  session.pause(1000);
  session.resume(2000);
  EXPECT_TRUE(session.isComplete());
  EXPECT_FALSE(session.isPaused());
  EXPECT_FALSE(session.isRunning());
}

TEST(BreathworkSession, PauseFreezesAndResumeRestartsOnlyTheUnfinishedCycle) {
  BreathworkSession session;
  ASSERT_TRUE(session.start(1000, BOX, 3));
  session.pause(22500);
  EXPECT_TRUE(session.isPaused());
  EXPECT_EQ(session.completedCycles(), 1u);
  EXPECT_EQ(session.phase(), Phase::Hold);
  EXPECT_EQ(session.remainingMs(100000), 26500u);
  EXPECT_EQ(session.remainingCounts(100000), 3u);
  EXPECT_EQ(session.phaseProgressPermille(100000), 375u);
  EXPECT_FALSE(session.tick(100000));
  session.pause(120000);
  EXPECT_EQ(session.remainingMs(120000), 26500u);
  session.resume(200000);
  EXPECT_TRUE(session.isRunning());
  EXPECT_EQ(session.phase(), Phase::Inhale);
  EXPECT_EQ(session.completedCycles(), 1u);
  EXPECT_EQ(session.remainingCounts(200000), 4u);
  EXPECT_EQ(session.remainingMs(200000), 32000u);
  EXPECT_EQ(session.phaseProgressPermille(200000), 0u);
  EXPECT_FALSE(session.tick(231999));
  EXPECT_TRUE(session.tick(232000));
  EXPECT_EQ(session.completedCycles(), 3u);
}

TEST(BreathworkSession, PauseAtCycleBoundaryKeepsTheCompletedCycle) {
  BreathworkSession session;
  ASSERT_TRUE(session.start(0, EQUAL, 2));
  session.pause(10000);
  EXPECT_EQ(session.completedCycles(), 1u);
  EXPECT_EQ(session.phase(), Phase::Inhale);
  session.resume(50000);
  EXPECT_EQ(session.remainingMs(50000), 10000u);
  EXPECT_TRUE(session.tick(60000));
}

TEST(BreathworkSession, PauseAtOrAfterDeadlineCompletesInsteadOfPausingZero) {
  for (const uint32_t pauseTime : {16000u, 16001u, 100000u}) {
    BreathworkSession session;
    ASSERT_TRUE(session.start(0, BOX, 1));
    session.pause(pauseTime);
    EXPECT_TRUE(session.isComplete());
    EXPECT_FALSE(session.isPaused());
    EXPECT_EQ(session.completedCycles(), 1u);
    EXPECT_EQ(session.remainingMs(pauseTime), 0u);
    session.resume(pauseTime + 1000);
    EXPECT_TRUE(session.isComplete());
    EXPECT_FALSE(session.tick(pauseTime + 1000));
  }
}

TEST(BreathworkSession, ResumeWhileRunningDoesNotExtendTheDeadline) {
  BreathworkSession session;
  ASSERT_TRUE(session.start(1000, BOX, 1));
  session.resume(10000);
  EXPECT_TRUE(session.tick(17000));
}

TEST(BreathworkSession, PhaseTimingAndCompletionSurviveMillisRollover) {
  BreathworkSession session;
  constexpr uint32_t start = UINT32_MAX - 999;
  ASSERT_TRUE(session.start(start, BOX, 1));
  EXPECT_FALSE(session.tick(0));
  EXPECT_EQ(session.remainingCounts(0), 3u);
  EXPECT_EQ(session.remainingMs(0), 15000u);
  EXPECT_FALSE(session.tick(3000));
  EXPECT_EQ(session.phase(), Phase::Hold);
  EXPECT_FALSE(session.tick(14999));
  EXPECT_EQ(session.remainingMs(14999), 1u);
  EXPECT_TRUE(session.tick(15000));
}

TEST(BreathworkSession, PauseAndResumeCanEachSpanMillisRollover) {
  BreathworkSession session;
  ASSERT_TRUE(session.start(UINT32_MAX - 999, BOX, 2));
  session.pause(20000);
  EXPECT_EQ(session.completedCycles(), 1u);
  EXPECT_EQ(session.phase(), Phase::Hold);
  session.resume(UINT32_MAX - 499);
  EXPECT_EQ(session.phase(), Phase::Inhale);
  EXPECT_EQ(session.remainingMs(0), 15500u);
  EXPECT_FALSE(session.tick(15499));
  EXPECT_TRUE(session.tick(15500));
}

TEST(BreathworkSession, InvalidConfigurationDoesNotDisturbPausedSession) {
  BreathworkSession session;
  ASSERT_TRUE(session.start(1000, BOX, 3));
  session.pause(21000);
  const uint8_t noInhale[4] = {0, 4, 4, 4};
  const uint8_t noExhale[4] = {4, 4, 0, 4};
  const uint8_t tooLong[4] = {4, 31, 4, 4};
  EXPECT_FALSE(session.start(30000, BOX, 0));
  EXPECT_FALSE(session.start(30000, BOX, 61));
  EXPECT_FALSE(session.start(30000, BOX, 1, 499));
  EXPECT_FALSE(session.start(30000, BOX, 1, 2001));
  EXPECT_FALSE(session.start(30000, noInhale, 1));
  EXPECT_FALSE(session.start(30000, noExhale, 1));
  EXPECT_FALSE(session.start(30000, tooLong, 1));
  EXPECT_TRUE(session.isPaused());
  EXPECT_EQ(session.phase(), Phase::Hold);
  EXPECT_EQ(session.totalCycles(), 3u);
  EXPECT_EQ(session.completedCycles(), 1u);
  EXPECT_EQ(session.totalMs(), 48000u);
  EXPECT_EQ(session.remainingMs(30000), 28000u);
  EXPECT_EQ(session.remainingCounts(30000), 4u);
}

TEST(BreathworkSession, MinimumAndMaximumConfigurationsStayInBounds) {
  BreathworkSession session;
  constexpr uint8_t minimum[4] = {1, 0, 1, 0};
  ASSERT_TRUE(session.start(0, minimum, 1, 500));
  EXPECT_EQ(session.totalMs(), 1000u);
  EXPECT_TRUE(session.tick(1000));
  constexpr uint8_t maximum[4] = {30, 30, 30, 30};
  ASSERT_TRUE(session.start(0, maximum, 60, 2000));
  EXPECT_EQ(session.totalMs(), 14400000u);
  EXPECT_FALSE(session.tick(14399999));
  EXPECT_EQ(session.remainingCounts(14399999), 1u);
  EXPECT_EQ(session.phaseProgressPermille(14399999), 999u);
  EXPECT_TRUE(session.tick(14400000));
}

TEST(BreathworkSession, NewSessionAndResetDiscardPreviousProgress) {
  BreathworkSession session;
  ASSERT_TRUE(session.start(0, BOX, 3));
  session.pause(20000);
  ASSERT_TRUE(session.start(30000, EQUAL, 2));
  EXPECT_TRUE(session.isRunning());
  EXPECT_EQ(session.completedCycles(), 0u);
  EXPECT_EQ(session.phase(), Phase::Inhale);
  EXPECT_EQ(session.totalMs(), 20000u);
  session.reset();
  EXPECT_FALSE(session.isRunning());
  EXPECT_FALSE(session.isPaused());
  EXPECT_FALSE(session.isComplete());
  EXPECT_EQ(session.totalMs(), 0u);
  EXPECT_EQ(session.totalCycles(), 0u);
  EXPECT_EQ(session.completedCycles(), 0u);
  EXPECT_EQ(session.remainingMs(40000), 0u);
  EXPECT_EQ(session.remainingCounts(40000), 0u);
  EXPECT_EQ(session.phaseProgressPermille(40000), 0u);
  EXPECT_FALSE(session.tick(40000));
  ASSERT_TRUE(session.start(50000, BOX, 1));
  EXPECT_TRUE(session.tick(66000));
}
