#include <gtest/gtest.h>
#include "encoder_factory.h"
using namespace droppix;

TEST(EncoderPrefParse, KnownAndUnknown) {
  EXPECT_EQ(parse_encoder_pref("auto"),     EncoderPref::Auto);
  EXPECT_EQ(parse_encoder_pref("nvenc"),    EncoderPref::Nvenc);
  EXPECT_EQ(parse_encoder_pref("vaapi"),    EncoderPref::Vaapi);
  EXPECT_EQ(parse_encoder_pref("software"), EncoderPref::Software);
  EXPECT_EQ(parse_encoder_pref("garbage"),  EncoderPref::Auto);   // default
}
TEST(EncoderOrder, AutoIsNvencVaapiSoftware) {
  EXPECT_EQ(select_encoder_order(EncoderPref::Auto),
            (std::vector<EncoderBackend>{EncoderBackend::Nvenc, EncoderBackend::Vaapi, EncoderBackend::Software}));
}
TEST(EncoderOrder, ForcedIsSingleton) {
  EXPECT_EQ(select_encoder_order(EncoderPref::Vaapi), (std::vector<EncoderBackend>{EncoderBackend::Vaapi}));
  EXPECT_EQ(select_encoder_order(EncoderPref::Software), (std::vector<EncoderBackend>{EncoderBackend::Software}));
}
