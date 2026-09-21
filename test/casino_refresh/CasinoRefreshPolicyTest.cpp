#include <gtest/gtest.h>

#include "activities/casino/CasinoRefreshPolicy.h"

TEST(CasinoRefreshPolicy, CleansOnlyTheFirstFrameOnEntry) {
  CasinoRefreshPolicy policy;
  EXPECT_TRUE(policy.nextFrameNeedsCleaning());
  EXPECT_FALSE(policy.nextFrameNeedsCleaning());
}

TEST(CasinoRefreshPolicy, LongRoundsNeverCleanUntilAnExplicitBoundary) {
  CasinoRefreshPolicy policy;
  ASSERT_TRUE(policy.nextFrameNeedsCleaning());

  for (unsigned frame = 0; frame < 4096; ++frame) {
    ASSERT_FALSE(policy.nextFrameNeedsCleaning()) << "Routine frame " << frame;
  }

  policy.allowCleaning();
  EXPECT_TRUE(policy.nextFrameNeedsCleaning());
  EXPECT_FALSE(policy.nextFrameNeedsCleaning());
}

TEST(CasinoRefreshPolicy, FrequentBoundariesDoNotCleanBeforeTheBudget) {
  CasinoRefreshPolicy policy;
  ASSERT_TRUE(policy.nextFrameNeedsCleaning());

  for (unsigned frame = 0; frame < CasinoRefreshPolicy::FAST_UPDATES_BEFORE_CLEAN; ++frame) {
    policy.allowCleaning();
    ASSERT_FALSE(policy.nextFrameNeedsCleaning()) << "Fast frame " << frame;
  }

  policy.allowCleaning();
  EXPECT_TRUE(policy.nextFrameNeedsCleaning());
}

TEST(CasinoRefreshPolicy, EarlyBoundaryDoesNotScheduleALaterMidRoundClean) {
  CasinoRefreshPolicy policy;
  ASSERT_TRUE(policy.nextFrameNeedsCleaning());

  for (unsigned frame = 0; frame < CasinoRefreshPolicy::FAST_UPDATES_BEFORE_CLEAN - 1; ++frame) {
    ASSERT_FALSE(policy.nextFrameNeedsCleaning());
  }
  policy.allowCleaning();
  ASSERT_FALSE(policy.nextFrameNeedsCleaning());
  EXPECT_FALSE(policy.nextFrameNeedsCleaning());

  policy.allowCleaning();
  EXPECT_TRUE(policy.nextFrameNeedsCleaning());
}

TEST(CasinoRefreshPolicy, RepeatedBoundaryCallsQueueOnlyOneCleanAndResetTheBudget) {
  CasinoRefreshPolicy policy;
  ASSERT_TRUE(policy.nextFrameNeedsCleaning());

  for (unsigned frame = 0; frame < CasinoRefreshPolicy::FAST_UPDATES_BEFORE_CLEAN; ++frame) {
    ASSERT_FALSE(policy.nextFrameNeedsCleaning());
  }
  policy.allowCleaning();
  policy.allowCleaning();
  policy.allowCleaning();
  ASSERT_TRUE(policy.nextFrameNeedsCleaning());

  for (unsigned frame = 0; frame < CasinoRefreshPolicy::FAST_UPDATES_BEFORE_CLEAN; ++frame) {
    policy.allowCleaning();
    ASSERT_FALSE(policy.nextFrameNeedsCleaning()) << "Fast frame after cleaning " << frame;
  }
  policy.allowCleaning();
  EXPECT_TRUE(policy.nextFrameNeedsCleaning());
}
