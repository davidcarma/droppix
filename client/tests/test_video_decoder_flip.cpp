#include "video_decoder.h"
#include <gtest/gtest.h>
TEST(VideoDecoderFlip, FormatCarriesMirror) {
  EXPECT_FALSE(droppix::make_frame_format(1280, 720, false).isMirrored());
  EXPECT_TRUE(droppix::make_frame_format(1280, 720, true).isMirrored());
}
