#include <gtest/gtest.h>
#include "stat_accumulator.h"

using droppix::StatAccumulator;

TEST(StatAccumulator, EmptyIsZero) {
  StatAccumulator s;
  EXPECT_EQ(s.count(), 0);
  EXPECT_DOUBLE_EQ(s.avg(), 0.0);
  EXPECT_DOUBLE_EQ(s.peak(), 0.0);
}

TEST(StatAccumulator, AvgPeakCount) {
  StatAccumulator s;
  s.add(10); s.add(20); s.add(30);
  EXPECT_EQ(s.count(), 3);
  EXPECT_DOUBLE_EQ(s.avg(), 20.0);
  EXPECT_DOUBLE_EQ(s.peak(), 30.0);
}

TEST(StatAccumulator, ResetClears) {
  StatAccumulator s;
  s.add(5); s.reset();
  EXPECT_EQ(s.count(), 0);
  EXPECT_DOUBLE_EQ(s.avg(), 0.0);
}
