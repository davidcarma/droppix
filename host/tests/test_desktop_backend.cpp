#include "desktop_backend.h"
#include <gtest/gtest.h>

using namespace droppix;

TEST(DesktopBackend, KdeDesktopSelectsKWin) {
  EXPECT_EQ(select_backend_kind("KDE", false), BackendKind::KWin);
}
TEST(DesktopBackend, PlasmaDesktopSelectsKWinCaseInsensitive) {
  EXPECT_EQ(select_backend_kind("plasma", false), BackendKind::KWin);
  EXPECT_EQ(select_backend_kind("KDE:plasmawayland", false), BackendKind::KWin);
}
TEST(DesktopBackend, UnknownDesktopWithKscreenSelectsKWin) {
  EXPECT_EQ(select_backend_kind("", true), BackendKind::KWin);
}
TEST(DesktopBackend, UnknownDesktopNoToolSelectsGeneric) {
  EXPECT_EQ(select_backend_kind("", false), BackendKind::Generic);
}
TEST(DesktopBackend, GnomeSelectsGeneric) {
  EXPECT_EQ(select_backend_kind("GNOME", false), BackendKind::Generic);
}
TEST(DesktopBackend, NonKdeDesktopIgnoresKscreenPresence) {
  // A named non-KDE desktop is Generic even if kscreen-doctor happens to be installed;
  // the tool only promotes an UNKNOWN desktop.
  EXPECT_EQ(select_backend_kind("sway", true), BackendKind::Generic);
}
