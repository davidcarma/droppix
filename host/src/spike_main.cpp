#include <csignal>
#include <cstdio>
#include <unistd.h>
#include "edid.h"
#include "virtual_display.h"

static volatile std::sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

int main() {
  std::signal(SIGINT, on_sigint);

  droppix::VirtualDisplay display;
  if (!display.open()) return 1;
  display.connect(droppix::build_edid(droppix::timing_1080p60()));

  std::fprintf(stderr,
      "Connected virtual monitor on evdi node %d.\n"
      "Check the host: `kscreen-doctor -o` should list a new output.\n"
      "Press Ctrl+C to disconnect.\n",
      display.node());

  while (!g_stop) pause();
  std::fprintf(stderr, "\nDisconnecting.\n");
  return 0;
}
