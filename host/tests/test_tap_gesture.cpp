#include "tap_gesture.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(TwoFingerTap, FiresRightClickAtMidpointOnQuietTwoFingerTap) {
  TwoFingerTap g;
  g.update({{0, 1000, 2000, 0}, {1, 3000, 4000, 0}}, 0);   // both fingers down
  auto r = g.update({}, 100);                               // both up, 100 ms later
  EXPECT_TRUE(r.rightClick);
  EXPECT_EQ(r.x, 2000);   // (1000 + 3000) / 2
  EXPECT_EQ(r.y, 3000);   // (2000 + 4000) / 2
}

TEST(TwoFingerTap, SingleFingerDoesNotClick) {
  TwoFingerTap g;
  g.update({{0, 1000, 2000, 0}}, 0);
  EXPECT_FALSE(g.update({}, 50).rightClick);
}

TEST(TwoFingerTap, MovementIsScrollNotClick) {
  TwoFingerTap g;
  g.update({{0, 1000, 2000, 0}, {1, 3000, 4000, 0}}, 0);
  g.update({{0, 1000, 9000, 0}, {1, 3000, 11000, 0}}, 50);   // moved > threshold
  EXPECT_FALSE(g.update({}, 100).rightClick);
}

TEST(TwoFingerTap, TooSlowIsNotATap) {
  TwoFingerTap g;
  g.update({{0, 1000, 2000, 0}, {1, 3000, 4000, 0}}, 0);
  EXPECT_FALSE(g.update({}, 1000).rightClick);   // 1 s > 400 ms
}

TEST(TwoFingerTap, ThreeFingersDoNotClick) {
  TwoFingerTap g;
  g.update({{0, 1, 1, 0}, {1, 2, 2, 0}, {2, 3, 3, 0}}, 0);
  EXPECT_FALSE(g.update({}, 50).rightClick);
}

TEST(TwoFingerTap, StaggeredTwoFingerStillTaps) {
  TwoFingerTap g;
  g.update({{0, 1000, 2000, 0}}, 0);                        // finger 1
  g.update({{0, 1000, 2000, 0}, {1, 3000, 4000, 0}}, 20);   // finger 2 -> count 2
  g.update({{0, 1000, 2000, 0}}, 40);                       // finger 2 up -> count 1
  auto r = g.update({}, 60);                                // finger 1 up
  EXPECT_TRUE(r.rightClick);
  EXPECT_EQ(r.x, 2000); EXPECT_EQ(r.y, 3000);
}
