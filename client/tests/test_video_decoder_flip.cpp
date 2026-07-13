#include "video_decoder.h"
#include <gtest/gtest.h>
TEST(VideoDecoderFlip, FormatCarriesMirror) {
  EXPECT_FALSE(droppix::make_frame_format(1280, 720, false).isMirrored());
  EXPECT_TRUE(droppix::make_frame_format(1280, 720, true).isMirrored());
}
TEST(AdjustLuma, NeutralIsIdentity) {
  for (int y : {0, 60, 128, 200, 255}) EXPECT_EQ(droppix::adjust_luma(y, 0, 100), y);
}
TEST(AdjustLuma, BrightnessShifts) {
  EXPECT_GT(droppix::adjust_luma(100, 50, 100), 100);
  EXPECT_LT(droppix::adjust_luma(150, -50, 100), 150);
}
TEST(AdjustLuma, ContrastAboutMid) {
  EXPECT_GT(droppix::adjust_luma(200, 0, 200), 200);          // >128 pushed up
  EXPECT_LT(droppix::adjust_luma(60, 0, 200), 60);            // <128 pushed down
  int lo = droppix::adjust_luma(200, 0, 50);
  EXPECT_GT(lo, 128); EXPECT_LT(lo, 200);                     // contrast<100 pulls toward 128
}
TEST(AdjustLuma, Clamps) {
  EXPECT_EQ(droppix::adjust_luma(250, 100, 200), 255);
  EXPECT_EQ(droppix::adjust_luma(10, -100, 100), 0);
}
