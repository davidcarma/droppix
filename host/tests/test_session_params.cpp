#include <gtest/gtest.h>
#include "session_params.h"
using namespace droppix;

TEST(SessionParams, V4PrefersClientValues) {
  auto p = select_session_params(4, /*fps*/60, /*audio*/1, /*orient*/2,
                                 /*def_fps*/30, /*def_audio*/false, /*def_orient*/0);
  EXPECT_EQ(p.fps, 60); EXPECT_TRUE(p.audio); EXPECT_EQ(p.orientation, 2);
}
TEST(SessionParams, V4ZeroFpsFallsBackToDefault) {
  auto p = select_session_params(4, 0, 0, 0, 30, true, 1);
  EXPECT_EQ(p.fps, 30);            // fps sentinel 0 -> default
  EXPECT_FALSE(p.audio);           // v4 audio flag is authoritative (client didn't ask)
  EXPECT_EQ(p.orientation, 0);
}
TEST(SessionParams, PreV4UsesDefaults) {
  auto p = select_session_params(3, 60, 1, 2, 24, true, 3);
  EXPECT_EQ(p.fps, 24); EXPECT_TRUE(p.audio); EXPECT_EQ(p.orientation, 3);
}
TEST(SessionParams, OrientationMaskedToTwoBits) {
  auto p = select_session_params(4, 30, 0, 7, 30, false, 0);
  EXPECT_EQ(p.orientation, 3);     // 7 & 3
}
