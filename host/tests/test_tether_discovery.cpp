#include <gtest/gtest.h>
#include "tether_discovery.h"

using namespace droppix;

TEST(TetherDiscovery, ProbeIsExactMagic) {
  auto p = encode_probe();
  ASSERT_EQ(p, (std::vector<unsigned char>{'D','P','X','Q'}));
  EXPECT_TRUE(is_probe(p));
  EXPECT_FALSE(is_probe({'D','P','X','R'}));
  EXPECT_FALSE(is_probe({'D','P','X'}));
}

TEST(TetherDiscovery, ReplyMatchesSharedVector) {
  TetherReply r; r.wake_port = 40000; r.id = "abc"; r.name = "Nexus 10";
  auto b = encode_reply(r);
  const std::vector<unsigned char> want = {
    0x44,0x50,0x58,0x52, 0x9C,0x40, 0x03,0x61,0x62,0x63,
    0x08,0x4E,0x65,0x78,0x75,0x73,0x20,0x31,0x30};
  EXPECT_EQ(b, want);
}

TEST(TetherDiscovery, ReplyRoundTrips) {
  TetherReply r; r.wake_port = 51234; r.id = "dev-xyz"; r.name = "Pixel";
  TetherReply out;
  ASSERT_TRUE(decode_reply(encode_reply(r), out));
  EXPECT_EQ(out.wake_port, 51234);
  EXPECT_EQ(out.id, "dev-xyz");
  EXPECT_EQ(out.name, "Pixel");
}

TEST(TetherDiscovery, DecodeRejectsBadMagicAndTruncation) {
  TetherReply out;
  EXPECT_FALSE(decode_reply({'X','X','X','X'}, out));               // bad magic
  EXPECT_FALSE(decode_reply({'D','P','X','R',0x9C}, out));          // too short for port
  // idLen says 5 but only 2 id bytes present:
  EXPECT_FALSE(decode_reply({'D','P','X','R',0x00,0x01,0x05,'a','b'}, out));
}
