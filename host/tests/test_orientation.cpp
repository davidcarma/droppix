#include <gtest/gtest.h>
#include "orientation.h"
using namespace droppix;

TEST(Orientation, CodeToDegrees) {
  EXPECT_EQ(orientation_degrees(0), 0);
  EXPECT_EQ(orientation_degrees(1), 90);
  EXPECT_EQ(orientation_degrees(2), 180);
  EXPECT_EQ(orientation_degrees(3), 270);
}
TEST(Orientation, UnknownCodeIsLandscape) {
  EXPECT_EQ(orientation_degrees(4), 0);
  EXPECT_EQ(orientation_degrees(255), 0);
}
