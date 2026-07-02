#include "approved_store.h"
#include <QTemporaryDir>
#include <gtest/gtest.h>

TEST(ApprovedStore, PersistsAcrossInstances) {
  QTemporaryDir d;
  { droppix::ApprovedStore s(d.path()); EXPECT_FALSE(s.isApproved("a")); s.approve("a"); }
  droppix::ApprovedStore s2(d.path());
  EXPECT_TRUE(s2.isApproved("a")); EXPECT_FALSE(s2.isApproved("b"));
  s2.clear(); EXPECT_FALSE(s2.isApproved("a"));
}

TEST(ApprovedStore, IdsListsAllApproved) {
  QTemporaryDir d;
  droppix::ApprovedStore s(d.path());
  s.approve("b"); s.approve("a"); s.approve("a");   // duplicate ignored
  EXPECT_EQ(s.ids(), (QStringList{"a", "b"}));       // sorted, deduped
}

TEST(ApprovedStore, RemoveForgetsOneAndPersists) {
  QTemporaryDir d;
  { droppix::ApprovedStore s(d.path()); s.approve("a"); s.approve("b"); s.remove("a"); }
  droppix::ApprovedStore s2(d.path());               // reload from disk
  EXPECT_FALSE(s2.isApproved("a"));
  EXPECT_TRUE(s2.isApproved("b"));
  EXPECT_EQ(s2.ids(), (QStringList{"b"}));
}
