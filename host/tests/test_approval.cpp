#include <gtest/gtest.h>
#include "approval.h"
using namespace droppix;

TEST(Approval, ParsesLines) {
  std::string id; bool a;
  ASSERT_TRUE(parse_approval("approve dev-1", id, a)); EXPECT_EQ(id,"dev-1"); EXPECT_TRUE(a);
  ASSERT_TRUE(parse_approval("deny dev-2", id, a)); EXPECT_EQ(id,"dev-2"); EXPECT_FALSE(a);
  EXPECT_FALSE(parse_approval("hello", id, a));
}
TEST(Approval, GateDeliversDecision) {
  ApprovalGate g; bool a=false;
  g.submit("x", true);
  ASSERT_TRUE(g.wait("x", 100, a)); EXPECT_TRUE(a);
}
TEST(Approval, GateTimesOut) {
  ApprovalGate g; bool a=false;
  EXPECT_FALSE(g.wait("missing", 50, a));
}
