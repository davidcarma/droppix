# Phase 0 — evdi Virtual-Display Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a host-side C++ program that creates a real virtual monitor via `evdi`/`libevdi` (which KWin auto-extends the desktop onto) and captures that monitor's pixels to PNG files — proving the entire capture foundation before any streaming work.

**Architecture:** A small C++17 program links the system `libevdi.so` (already on the host). It builds a valid EDID, opens/creates an evdi device, connects with that EDID so KWin sees a new monitor, registers a framebuffer, and runs an event/grab loop to pull dirty-rect pixel updates. A spike CLI saves captured frames to PNG. All building and running happens inside a Fedora `distrobox` (the base OS is immutable), which shares the host kernel module and `/dev` nodes.

**Tech Stack:** C++17, CMake (≥3.20), `libevdi` 1.14.16, GoogleTest (via CMake FetchContent), `stb_image_write` (vendored single-header) for PNG output, `distrobox` (Fedora) build container.

## Global Constraints

These apply to every task. Values copied verbatim from the spec and verified on the dev machine.

- **Target libevdi API:** 1.14.16. Use `evdi_connect2` (not the older `evdi_connect`). The **vendored `evdi_lib.h` is the source of truth** for signatures — if any prototype in this plan differs from the header, follow the header.
- **Language standard:** C++17.
- **Build/run environment:** a Fedora `distrobox` named `droppix-dev`. The host is immutable Bazzite (Fedora 44 / Kinoite); do **not** layer packages onto the base image. The container shares the host's loaded `evdi` module and `/dev/evdi*` nodes.
- **Compositor target:** KDE Plasma 6.6.5 / KWin 6.6.5, Wayland session. Verification of "a monitor appeared" uses `kscreen-doctor -o` on the **host** (not the container).
- **Working name:** `droppix`. Repo root is the current project directory; all host code lives under `host/`.
- **Privilege note:** `evdi_add_device()` writes to sysfs and may require root. The spike is allowed to run via `sudo` for now; an unprivileged udev rule is deferred to a later phase.
- **Latency priority:** capture uses dirty rectangles (never assume full-frame); this is foundational for later low-latency encoding.

---

## File Structure

```
host/
  CMakeLists.txt                 # build + FetchContent GoogleTest, link libevdi
  third_party/
    evdi/evdi_lib.h              # vendored libevdi 1.14.16 header (source of truth)
    stb/stb_image_write.h        # vendored single-header PNG writer
  src/
    edid.h  edid.cpp             # pure: build a valid 128-byte EDID from a Timing
    virtual_display.h .cpp       # libevdi wrapper: open/add device, connect/disconnect
    capturer.h .cpp              # buffer registration + event/grab loop -> Frame
    spike_main.cpp               # CLI: create monitor, capture N frames -> PNG
  tests/
    test_edid.cpp                # GoogleTest for the EDID builder
scripts/
  dev-container.sh               # create/enter the droppix-dev distrobox
```

Each file has one responsibility: `edid` is pure data encoding (fully unit-tested), `virtual_display` owns the evdi handle lifecycle, `capturer` owns the framebuffer + grab loop, `spike_main` wires them into a runnable proof.

---

### Task 1: Dev container, project scaffold, and a passing smoke test

**Files:**
- Create: `scripts/dev-container.sh`
- Create: `host/CMakeLists.txt`
- Create: `host/third_party/evdi/evdi_lib.h` (vendored)
- Create: `host/third_party/stb/stb_image_write.h` (vendored)
- Create: `host/tests/test_smoke.cpp`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: a working CMake project that builds with C++17, links `libevdi`, and runs GoogleTest via `ctest`. Later tasks add sources to the `droppix_core` library and tests to the `droppix_tests` target.

- [ ] **Step 1: Create the dev container helper script**

Create `scripts/dev-container.sh`:

```bash
#!/usr/bin/env bash
# Create and enter the droppix-dev Fedora distrobox.
# The container shares the host kernel's evdi module and /dev nodes.
set -euo pipefail

NAME=droppix-dev
IMAGE=registry.fedoraproject.org/fedora:44

if ! distrobox list | grep -q "\b${NAME}\b"; then
  distrobox create --name "${NAME}" --image "${IMAGE}" --yes
fi

# Install build dependencies inside the container (idempotent).
distrobox enter "${NAME}" -- bash -lc '
  sudo dnf install -y gcc-c++ cmake git libevdi kscreen 2>/dev/null || \
  sudo dnf install -y gcc-c++ cmake git libevdi
'

echo "Container ready. Enter it with: distrobox enter ${NAME}"
```

Make it executable and run it:

```bash
chmod +x scripts/dev-container.sh
./scripts/dev-container.sh
```

Expected: container `droppix-dev` is created and dependencies install. (`libevdi` provides `/usr/lib64/libevdi.so` for linking.)

- [ ] **Step 2: Vendor the libevdi header**

Fetch the exact-version header into the repo (run from repo root, inside or outside the container — needs network):

```bash
mkdir -p host/third_party/evdi host/third_party/stb
curl -fsSL -o host/third_party/evdi/evdi_lib.h \
  https://raw.githubusercontent.com/DisplayLink/evdi/v1.14.16/library/evdi_lib.h
```

Verify it declares the functions this plan uses:

```bash
grep -E "evdi_connect2|evdi_register_buffer|evdi_grab_pixels|evdi_handle_events|evdi_get_event_ready|evdi_request_update|evdi_add_device|evdi_check_device" host/third_party/evdi/evdi_lib.h
```

Expected: each symbol appears. **If `evdi_connect2` is absent in the fetched header, stop and reconcile** — confirm the installed `libevdi` version with `nm -D /lib64/libevdi.so.1 | grep evdi_connect2` and fetch the matching tag.

- [ ] **Step 3: Vendor the PNG writer header**

```bash
curl -fsSL -o host/third_party/stb/stb_image_write.h \
  https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
test -s host/third_party/stb/stb_image_write.h && echo OK
```

Expected: `OK`.

- [ ] **Step 4: Write the CMake project**

Create `host/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(droppix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Locate the system libevdi runtime library (header is vendored).
find_library(EVDI_LIB NAMES evdi PATHS /usr/lib64 /lib64 REQUIRED)
message(STATUS "Using libevdi: ${EVDI_LIB}")

# Core library: sources are added by later tasks.
add_library(droppix_core
  src/edid.cpp
)
target_include_directories(droppix_core PUBLIC
  src
  third_party/evdi
  third_party/stb
)
target_link_libraries(droppix_core PUBLIC ${EVDI_LIB})

# --- Tests (GoogleTest via FetchContent) ---
include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(droppix_tests
  tests/test_smoke.cpp
)
target_link_libraries(droppix_tests PRIVATE droppix_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(droppix_tests)
```

Note: `src/edid.cpp` is referenced now but created in Task 2. For this task only, create a temporary empty stub so the build succeeds:

```bash
mkdir -p host/src
printf '// placeholder, implemented in Task 2\n' > host/src/edid.cpp
```

- [ ] **Step 5: Write the smoke test**

Create `host/tests/test_smoke.cpp`:

```cpp
#include <gtest/gtest.h>

TEST(Smoke, ToolchainWorks) {
  EXPECT_EQ(2 + 2, 4);
}
```

- [ ] **Step 6: Configure and build inside the container**

Run from the repo root inside `distrobox enter droppix-dev`:

```bash
cmake -S host -B host/build -DCMAKE_BUILD_TYPE=Debug
cmake --build host/build -j
```

Expected: configure downloads GoogleTest and builds with no errors; `droppix_tests` links against libevdi.

- [ ] **Step 7: Run the smoke test**

```bash
ctest --test-dir host/build --output-on-failure
```

Expected: `1 test from Smoke` PASS, `100% tests passed`.

- [ ] **Step 8: Commit**

```bash
git add host/ scripts/ docs/
git commit -m "build: scaffold host project, dev container, vendored evdi/stb headers"
```

---

### Task 2: EDID builder (pure logic, TDD)

**Files:**
- Create: `host/src/edid.h`
- Modify: `host/src/edid.cpp` (replace the Task 1 placeholder)
- Create: `host/tests/test_edid.cpp`
- Modify: `host/CMakeLists.txt` (add `tests/test_edid.cpp` to `droppix_tests`)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct droppix::Timing { int pixel_clock_khz; int h_active, h_front, h_sync, h_blank; int v_active, v_front, v_sync, v_blank; int h_mm, v_mm; };`
  - `droppix::Timing droppix::timing_1080p60();` — returns the CEA-861 1920x1080@60 timing (148500 kHz; h: active 1920, front 88, sync 44, blank 280; v: active 1080, front 4, sync 5, blank 45; 480x270 mm).
  - `std::vector<unsigned char> droppix::build_edid(const Timing& t);` — returns a 128-byte EDID 1.3 block whose checksum byte makes the block sum to 0 (mod 256), encoding `t` as detailed timing descriptor #1.

- [ ] **Step 1: Write the failing tests**

Create `host/tests/test_edid.cpp`:

```cpp
#include <gtest/gtest.h>
#include <numeric>
#include "edid.h"

using droppix::build_edid;
using droppix::timing_1080p60;

TEST(Edid, IsExactly128Bytes) {
  EXPECT_EQ(build_edid(timing_1080p60()).size(), 128u);
}

TEST(Edid, HasFixedHeaderPattern) {
  auto e = build_edid(timing_1080p60());
  const unsigned char header[8] = {0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00};
  for (int i = 0; i < 8; ++i) EXPECT_EQ(e[i], header[i]) << "byte " << i;
}

TEST(Edid, ChecksumMakesBlockSumZeroMod256) {
  auto e = build_edid(timing_1080p60());
  int sum = std::accumulate(e.begin(), e.end(), 0);
  EXPECT_EQ(sum % 256, 0);
}

TEST(Edid, Version1Point3) {
  auto e = build_edid(timing_1080p60());
  EXPECT_EQ(e[18], 0x01);  // EDID version
  EXPECT_EQ(e[19], 0x03);  // revision 3
}

TEST(Edid, FirstDetailedTimingEncodesActivePixels) {
  auto e = build_edid(timing_1080p60());
  // Detailed Timing Descriptor #1 starts at byte 54.
  const int o = 54;
  int h_active = e[o+2] | ((e[o+4] & 0xF0) << 4);
  int v_active = e[o+5] | ((e[o+7] & 0xF0) << 4);
  EXPECT_EQ(h_active, 1920);
  EXPECT_EQ(v_active, 1080);
}

TEST(Edid, PixelClockEncodedLittleEndianIn10kHzUnits) {
  auto e = build_edid(timing_1080p60());
  const int o = 54;
  int clk = e[o] | (e[o+1] << 8);   // units of 10 kHz
  EXPECT_EQ(clk, 14850);            // 148.5 MHz
}
```

- [ ] **Step 2: Add the test file to CMake and run to verify it fails**

In `host/CMakeLists.txt`, change the `droppix_tests` sources to include the new file:

```cmake
add_executable(droppix_tests
  tests/test_smoke.cpp
  tests/test_edid.cpp
)
```

Then:

```bash
cmake --build host/build -j
```

Expected: **compile failure** — `edid.h` does not exist yet. (That is the failing state for this step.)

- [ ] **Step 3: Write the header**

Create `host/src/edid.h`:

```cpp
#pragma once
#include <vector>

namespace droppix {

struct Timing {
  int pixel_clock_khz;            // e.g. 148500
  int h_active, h_front, h_sync, h_blank;  // pixels
  int v_active, v_front, v_sync, v_blank;  // lines
  int h_mm, v_mm;                 // physical image size in millimetres
};

// CEA-861 1920x1080 @ 60 Hz.
Timing timing_1080p60();

// Build a 128-byte EDID 1.3 block encoding `t` as Detailed Timing #1.
// The final checksum byte makes the whole block sum to 0 (mod 256).
std::vector<unsigned char> build_edid(const Timing& t);

}  // namespace droppix
```

- [ ] **Step 4: Write the implementation**

Replace the contents of `host/src/edid.cpp`:

```cpp
#include "edid.h"
#include <array>

namespace droppix {

Timing timing_1080p60() {
  return Timing{
      /*pixel_clock_khz*/ 148500,
      /*h_active*/ 1920, /*h_front*/ 88,  /*h_sync*/ 44, /*h_blank*/ 280,
      /*v_active*/ 1080, /*v_front*/ 4,   /*v_sync*/ 5,  /*v_blank*/ 45,
      /*h_mm*/ 480,      /*v_mm*/ 270};
}

static void write_dtd(unsigned char* d, const Timing& t) {
  const int clk = t.pixel_clock_khz / 10;  // 10 kHz units
  d[0] = clk & 0xFF;
  d[1] = (clk >> 8) & 0xFF;

  d[2] = t.h_active & 0xFF;
  d[3] = t.h_blank & 0xFF;
  d[4] = ((t.h_active >> 8) & 0x0F) << 4 | ((t.h_blank >> 8) & 0x0F);

  d[5] = t.v_active & 0xFF;
  d[6] = t.v_blank & 0xFF;
  d[7] = ((t.v_active >> 8) & 0x0F) << 4 | ((t.v_blank >> 8) & 0x0F);

  d[8] = t.h_front & 0xFF;
  d[9] = t.h_sync & 0xFF;
  d[10] = ((t.v_front & 0x0F) << 4) | (t.v_sync & 0x0F);
  d[11] = ((t.h_front >> 8) & 0x03) << 6 |
          ((t.h_sync  >> 8) & 0x03) << 4 |
          ((t.v_front >> 4) & 0x03) << 2 |
          ((t.v_sync  >> 4) & 0x03);

  d[12] = t.h_mm & 0xFF;
  d[13] = t.v_mm & 0xFF;
  d[14] = ((t.h_mm >> 8) & 0x0F) << 4 | ((t.v_mm >> 8) & 0x0F);
  d[15] = 0;  // h border
  d[16] = 0;  // v border
  d[17] = 0x1E;  // digital separate sync, +h +v
}

std::vector<unsigned char> build_edid(const Timing& t) {
  std::array<unsigned char, 128> e{};  // zero-initialised

  // Header (bytes 0-7).
  const unsigned char header[8] = {0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00};
  for (int i = 0; i < 8; ++i) e[i] = header[i];

  // Manufacturer ID "DPX" (bytes 8-9), 5-bit packed big-endian.
  // 'D'=4,'P'=16,'X'=24 -> (4<<10)|(16<<5)|24 = 0x1118.
  e[8]  = 0x11;
  e[9]  = 0x18;
  // Product code (10-11), serial (12-15) left as 0/defaults.
  e[10] = 0x01; e[11] = 0x00;
  e[16] = 0x01;  // week
  e[17] = 0x21;  // year 2023 (1990 + 0x21)

  e[18] = 0x01;  // EDID version 1
  e[19] = 0x03;  // revision 3

  // Basic display params (byte 20: digital input).
  e[20] = 0x80;            // digital
  e[21] = t.h_mm / 10;     // max horizontal image size (cm)
  e[22] = t.v_mm / 10;     // max vertical image size (cm)
  e[23] = 0x78;            // gamma 2.2
  e[24] = 0x0A;            // RGB, preferred timing = DTD#1

  // Chromaticity (25-34): generic sRGB-ish values.
  const unsigned char chroma[10] = {
      0xEE,0x91,0xA3,0x54,0x4C,0x99,0x26,0x0F,0x50,0x54};
  for (int i = 0; i < 10; ++i) e[25 + i] = chroma[i];

  // Established/standard timings (35-53): none required, leave zero/unused.
  for (int i = 38; i <= 53; ++i) e[i] = 0x01;  // unused standard timing markers

  // Detailed Timing Descriptor #1 (bytes 54-71).
  write_dtd(&e[54], t);

  // Descriptor #2 (72-89): monitor name "droppix".
  e[72] = 0; e[73] = 0; e[74] = 0; e[75] = 0xFC; e[76] = 0;
  const char* name = "droppix";
  int p = 77;
  for (const char* c = name; *c && p < 90; ++c) e[p++] = *c;
  while (p < 90) e[p++] = (p == 77) ? 0x0A : 0x20;  // pad with spaces, LF-terminate

  // Descriptors #3 (90-107) and #4 (108-125): dummy.
  e[91] = e[109] = 0x10;  // dummy descriptor tag

  e[126] = 0;  // extension count

  // Checksum (byte 127): make the block sum to 0 mod 256.
  int sum = 0;
  for (int i = 0; i < 127; ++i) sum += e[i];
  e[127] = static_cast<unsigned char>((256 - (sum % 256)) % 256);

  return std::vector<unsigned char>(e.begin(), e.end());
}

}  // namespace droppix
```

- [ ] **Step 5: Build and run the tests to verify they pass**

```bash
cmake --build host/build -j && ctest --test-dir host/build --output-on-failure
```

Expected: all `Edid.*` tests and `Smoke.*` PASS.

- [ ] **Step 6: Commit**

```bash
git add host/src/edid.h host/src/edid.cpp host/tests/test_edid.cpp host/CMakeLists.txt
git commit -m "feat(edid): pure EDID 1.3 builder with 1080p60 timing"
```

---

### Task 3: VirtualDisplay — open/create an evdi device and connect with EDID

**Files:**
- Create: `host/src/virtual_display.h`
- Create: `host/src/virtual_display.cpp`
- Modify: `host/CMakeLists.txt` (add `src/virtual_display.cpp` to `droppix_core`)
- Create: `host/src/spike_main.cpp` (minimal CLI for this task; extended in Task 5)
- Modify: `host/CMakeLists.txt` (add `droppix_spike` executable)

**Interfaces:**
- Consumes: `droppix::build_edid`, `droppix::timing_1080p60` (Task 2).
- Produces:
  - `class droppix::VirtualDisplay` with:
    - `bool open();` — finds an `AVAILABLE` evdi node (scanning 0..15) or calls `evdi_add_device()` then rescans; `evdi_open`s it. Returns success.
    - `void connect(const std::vector<unsigned char>& edid);` — calls `evdi_connect2(handle, edid.data(), edid.size(), 0, 0)`.
    - `void disconnect();` and destructor — `evdi_disconnect` + `evdi_close`.
    - `int node() const;` — the evdi device index opened.
    - `evdi_handle handle() const;` — raw handle for the Capturer (Task 4).

- [ ] **Step 1: Write the header**

Create `host/src/virtual_display.h`:

```cpp
#pragma once
#include <vector>
#include "evdi_lib.h"

namespace droppix {

class VirtualDisplay {
 public:
  ~VirtualDisplay();
  bool open();
  void connect(const std::vector<unsigned char>& edid);
  void disconnect();
  int node() const { return node_; }
  evdi_handle handle() const { return handle_; }

 private:
  evdi_handle handle_ = nullptr;
  int node_ = -1;
  bool connected_ = false;
};

}  // namespace droppix
```

- [ ] **Step 2: Write the implementation**

Create `host/src/virtual_display.cpp`:

```cpp
#include "virtual_display.h"
#include <cstdio>

namespace droppix {

static int find_available_node() {
  for (int i = 0; i < 16; ++i) {
    if (evdi_check_device(i) == AVAILABLE) return i;
  }
  return -1;
}

bool VirtualDisplay::open() {
  node_ = find_available_node();
  if (node_ < 0) {
    // No free node: ask the kernel to add one (may require root).
    if (evdi_add_device() < 0) {
      std::fprintf(stderr, "evdi_add_device failed (try running with sudo)\n");
      return false;
    }
    node_ = find_available_node();
  }
  if (node_ < 0) {
    std::fprintf(stderr, "no AVAILABLE evdi node found\n");
    return false;
  }
  handle_ = evdi_open(node_);
  if (handle_ == EVDI_INVALID_HANDLE) {
    std::fprintf(stderr, "evdi_open(%d) failed\n", node_);
    handle_ = nullptr;
    return false;
  }
  std::fprintf(stderr, "opened evdi node %d\n", node_);
  return true;
}

void VirtualDisplay::connect(const std::vector<unsigned char>& edid) {
  // pixel_area_limit / pixel_per_second_limit = 0 means "no limit".
  evdi_connect2(handle_, edid.data(),
                static_cast<unsigned>(edid.size()), 0, 0);
  connected_ = true;
}

void VirtualDisplay::disconnect() {
  if (connected_ && handle_) {
    evdi_disconnect(handle_);
    connected_ = false;
  }
}

VirtualDisplay::~VirtualDisplay() {
  disconnect();
  if (handle_) evdi_close(handle_);
}

}  // namespace droppix
```

Note: if the vendored header names the invalid handle differently than `EVDI_INVALID_HANDLE`, use the header's spelling (check `grep INVALID host/third_party/evdi/evdi_lib.h`; older headers use `EVDI_INVALID_HANDLE`).

- [ ] **Step 3: Write a minimal spike CLI to exercise connect**

Create `host/src/spike_main.cpp`:

```cpp
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
```

- [ ] **Step 4: Wire CMake**

In `host/CMakeLists.txt`, add the source to the core library:

```cmake
add_library(droppix_core
  src/edid.cpp
  src/virtual_display.cpp
)
```

And add the spike executable (after the `droppix_core` target):

```cmake
add_executable(droppix_spike src/spike_main.cpp)
target_link_libraries(droppix_spike PRIVATE droppix_core)
```

- [ ] **Step 5: Build**

```bash
cmake --build host/build -j
```

Expected: `droppix_spike` builds and links against libevdi.

- [ ] **Step 6: Run and verify a monitor appears (integration check)**

First capture the baseline output count on the **host** (run in a host terminal, not the container):

```bash
kscreen-doctor -o | grep -c "^Output:"
```

Note the number. Then, inside the container, run the spike (with `sudo` so `evdi_add_device` can write sysfs):

```bash
sudo host/build/droppix_spike
```

While it runs, in the host terminal again:

```bash
kscreen-doctor -o | grep -c "^Output:"
```

Expected: the count increased by 1, and `kscreen-doctor -o` shows an output named `droppix` (or a new evdi output). In System Settings → Display, a new 1920x1080 monitor is present and can be arranged. Press Ctrl+C to stop; the output disappears.

**If `evdi_add_device` fails even with sudo:** confirm the module is loaded (`lsmod | grep evdi`) and that `/sys/devices/platform/evdi` exists; record the failure for the spike report rather than forcing it.

- [ ] **Step 7: Commit**

```bash
git add host/src/virtual_display.h host/src/virtual_display.cpp host/src/spike_main.cpp host/CMakeLists.txt
git commit -m "feat(display): create + connect an evdi virtual monitor"
```

---

### Task 4: Capturer — register a framebuffer and grab dirty-rect updates

**Files:**
- Create: `host/src/capturer.h`
- Create: `host/src/capturer.cpp`
- Modify: `host/CMakeLists.txt` (add `src/capturer.cpp` to `droppix_core`)

**Interfaces:**
- Consumes: `droppix::VirtualDisplay::handle()` (Task 3).
- Produces:
  - `struct droppix::Frame { int width=0, height=0, stride=0; std::vector<unsigned char> bgra; std::vector<evdi_rect> rects; bool valid=false; };`
  - `class droppix::Capturer` with:
    - `explicit Capturer(evdi_handle h);`
    - `bool wait_for_mode(int timeout_ms);` — pumps evdi events until a `mode_changed` arrives; allocates + registers the buffer for the new mode. Returns whether a mode was received.
    - `Frame grab(int timeout_ms);` — requests an update, pumps events, and on `update_ready` calls `evdi_grab_pixels`, returning a `Frame` with the changed rectangles (or `valid=false` on timeout).
    - `int width() const; int height() const;`

- [ ] **Step 1: Write the header**

Create `host/src/capturer.h`:

```cpp
#pragma once
#include <vector>
#include "evdi_lib.h"

namespace droppix {

struct Frame {
  int width = 0, height = 0, stride = 0;
  std::vector<unsigned char> bgra;   // 32bpp, B,G,R,X byte order
  std::vector<evdi_rect> rects;      // changed regions
  bool valid = false;
};

class Capturer {
 public:
  explicit Capturer(evdi_handle h);
  ~Capturer();
  bool wait_for_mode(int timeout_ms);
  Frame grab(int timeout_ms);
  int width() const { return width_; }
  int height() const { return height_; }

 private:
  bool wait_readable(int timeout_ms);
  void register_buffer();

  evdi_handle handle_;
  int width_ = 0, height_ = 0, stride_ = 0;
  int buffer_id_ = 1;
  std::vector<unsigned char> buffer_;
  bool buffer_registered_ = false;

  // event-loop scratch state, written by static handlers
  bool got_mode_ = false;
  bool update_ready_ = false;
  static void on_mode_changed(evdi_mode mode, void* user);
  static void on_update_ready(int buf, void* user);
};

}  // namespace droppix
```

- [ ] **Step 2: Write the implementation**

Create `host/src/capturer.cpp`:

```cpp
#include "capturer.h"
#include <poll.h>
#include <cstdio>
#include <cstring>

namespace droppix {

Capturer::Capturer(evdi_handle h) : handle_(h) {}

Capturer::~Capturer() {
  if (buffer_registered_) evdi_unregister_buffer(handle_, buffer_id_);
}

void Capturer::on_mode_changed(evdi_mode mode, void* user) {
  auto* self = static_cast<Capturer*>(user);
  self->width_ = mode.width;
  self->height_ = mode.height;
  self->stride_ = mode.width * 4;  // 32bpp
  self->got_mode_ = true;
  std::fprintf(stderr, "mode changed: %dx%d @ %d bpp\n",
               mode.width, mode.height, mode.bits_per_pixel);
}

void Capturer::on_update_ready(int /*buf*/, void* user) {
  static_cast<Capturer*>(user)->update_ready_ = true;
}

bool Capturer::wait_readable(int timeout_ms) {
  struct pollfd pfd{evdi_get_event_ready(handle_), POLLIN, 0};
  return poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN);
}

void Capturer::register_buffer() {
  buffer_.assign(static_cast<size_t>(stride_) * height_, 0);
  evdi_buffer b{};
  b.id = buffer_id_;
  b.buffer = buffer_.data();
  b.width = width_;
  b.height = height_;
  b.stride = stride_;
  b.rects = nullptr;     // filled by evdi_grab_pixels
  b.rect_count = 0;
  evdi_register_buffer(handle_, b);
  buffer_registered_ = true;
}

bool Capturer::wait_for_mode(int timeout_ms) {
  evdi_event_context ctx{};
  ctx.mode_changed_handler = &Capturer::on_mode_changed;
  ctx.user_data = this;
  got_mode_ = false;
  while (!got_mode_) {
    if (!wait_readable(timeout_ms)) return false;
    evdi_handle_events(handle_, &ctx);
  }
  register_buffer();
  return true;
}

Frame Capturer::grab(int timeout_ms) {
  Frame f;
  if (!buffer_registered_) return f;

  evdi_event_context ctx{};
  ctx.update_ready_handler = &Capturer::on_update_ready;
  ctx.mode_changed_handler = &Capturer::on_mode_changed;
  ctx.user_data = this;

  update_ready_ = false;
  // If the update is immediately ready, evdi_request_update returns true.
  bool ready = evdi_request_update(handle_, buffer_id_);
  if (!ready) {
    if (!wait_readable(timeout_ms)) return f;
    evdi_handle_events(handle_, &ctx);
    if (!update_ready_) return f;
  }

  evdi_rect rects[16];
  int num = 0;
  evdi_grab_pixels(handle_, rects, &num);

  f.width = width_;
  f.height = height_;
  f.stride = stride_;
  f.bgra = buffer_;  // copy current contents
  f.rects.assign(rects, rects + num);
  f.valid = true;
  return f;
}

}  // namespace droppix
```

Note: confirm against the vendored header whether `evdi_request_update` returns `bool` and the exact `evdi_grab_pixels` signature; adjust if the header differs.

- [ ] **Step 3: Wire CMake**

In `host/CMakeLists.txt` add to the core library:

```cmake
add_library(droppix_core
  src/edid.cpp
  src/virtual_display.cpp
  src/capturer.cpp
)
```

- [ ] **Step 4: Build to verify it compiles against the real header**

```bash
cmake --build host/build -j
```

Expected: `droppix_core` compiles and links. (Behavioral verification happens in Task 5, where the loop is driven end-to-end and frames are saved — capture depends on the live evdi device, so it is integration-tested there rather than unit-tested.)

- [ ] **Step 5: Commit**

```bash
git add host/src/capturer.h host/src/capturer.cpp host/CMakeLists.txt
git commit -m "feat(capture): evdi buffer registration + dirty-rect grab loop"
```

---

### Task 5: Spike CLI — capture the virtual monitor to PNG (end-to-end proof)

**Files:**
- Modify: `host/src/spike_main.cpp` (extend Task 3's CLI to capture and save PNGs)
- Create: `host/src/png_writer.h` (thin wrapper around stb)
- Create: `host/src/png_writer.cpp` (defines `STB_IMAGE_WRITE_IMPLEMENTATION`)
- Modify: `host/CMakeLists.txt` (add `src/png_writer.cpp` to `droppix_core`)

**Interfaces:**
- Consumes: `VirtualDisplay`, `Capturer`, `Frame`, `build_edid`, `timing_1080p60`.
- Produces: `bool droppix::save_png_from_bgra(const std::string& path, const Frame& f);` — converts BGRA→RGBA and writes a PNG. The `droppix_spike` binary becomes the runnable proof.

- [ ] **Step 1: Write the PNG writer header**

Create `host/src/png_writer.h`:

```cpp
#pragma once
#include <string>
#include "capturer.h"

namespace droppix {
// Writes frame `f` (BGRA) to `path` as RGBA PNG. Returns success.
bool save_png_from_bgra(const std::string& path, const Frame& f);
}
```

- [ ] **Step 2: Write the PNG writer implementation**

Create `host/src/png_writer.cpp`:

```cpp
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "png_writer.h"
#include <vector>

namespace droppix {

bool save_png_from_bgra(const std::string& path, const Frame& f) {
  if (!f.valid || f.bgra.empty()) return false;
  std::vector<unsigned char> rgba(f.bgra.size());
  for (size_t i = 0; i + 3 < f.bgra.size(); i += 4) {
    rgba[i + 0] = f.bgra[i + 2];  // R <- B
    rgba[i + 1] = f.bgra[i + 1];  // G
    rgba[i + 2] = f.bgra[i + 0];  // B <- R
    rgba[i + 3] = 0xFF;           // opaque
  }
  return stbi_write_png(path.c_str(), f.width, f.height, 4,
                        rgba.data(), f.stride) != 0;
}

}  // namespace droppix
```

- [ ] **Step 3: Extend the spike CLI to capture frames**

Replace the contents of `host/src/spike_main.cpp`:

```cpp
#include <csignal>
#include <cstdio>
#include <string>
#include <unistd.h>
#include "edid.h"
#include "virtual_display.h"
#include "capturer.h"
#include "png_writer.h"

static volatile std::sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

int main(int argc, char** argv) {
  std::signal(SIGINT, on_sigint);
  const int frames = (argc > 1) ? std::atoi(argv[1]) : 10;

  droppix::VirtualDisplay display;
  if (!display.open()) return 1;
  display.connect(droppix::build_edid(droppix::timing_1080p60()));
  std::fprintf(stderr, "Connected on evdi node %d. Waiting for KWin mode...\n",
               display.node());

  droppix::Capturer cap(display.handle());
  if (!cap.wait_for_mode(5000)) {
    std::fprintf(stderr, "No mode within 5s. Is KWin extending onto it?\n");
    return 2;
  }
  std::fprintf(stderr,
      "Mode %dx%d. Drag a window onto the new monitor, then watch frames.\n",
      cap.width(), cap.height());

  int saved = 0;
  for (int i = 0; i < frames && !g_stop; ++i) {
    droppix::Frame f = cap.grab(1000);
    if (!f.valid) { std::fprintf(stderr, "frame %d: timeout\n", i); continue; }
    std::string path = "frame_" + std::to_string(i) + ".png";
    if (droppix::save_png_from_bgra(path, f)) {
      std::fprintf(stderr, "saved %s (%zu dirty rects)\n",
                   path.c_str(), f.rects.size());
      ++saved;
    }
    usleep(200 * 1000);  // 5 fps sampling for the spike
  }
  std::fprintf(stderr, "Done. %d/%d frames saved.\n", saved, frames);
  return saved > 0 ? 0 : 3;
}
```

- [ ] **Step 4: Wire CMake**

In `host/CMakeLists.txt` add `src/png_writer.cpp` to the core library:

```cmake
add_library(droppix_core
  src/edid.cpp
  src/virtual_display.cpp
  src/capturer.cpp
  src/png_writer.cpp
)
```

- [ ] **Step 5: Build**

```bash
cmake --build host/build -j
```

Expected: `droppix_spike` builds.

- [ ] **Step 6: Run the full end-to-end proof**

Inside the container, from a writable directory:

```bash
cd /tmp && sudo /path/to/host/build/droppix_spike 10
```

While it runs: on the host, open System Settings → Display, confirm the new `droppix` monitor, set it to extend, and drag a window (e.g. a terminal or a colourful app) onto it.

Expected:
- `mode 1920x1080` is reported.
- Several `saved frame_N.png (... dirty rects)` lines appear.
- Opening the PNGs shows the actual content of the virtual monitor (the window you dragged over), with correct colours.

This is the Phase 0 success criterion: **a virtual monitor that KWin extends onto, captured to disk via dirty-rect updates.**

- [ ] **Step 7: Commit**

```bash
git add host/src/png_writer.h host/src/png_writer.cpp host/src/spike_main.cpp host/CMakeLists.txt
git commit -m "feat(spike): capture evdi virtual monitor to PNG end-to-end"
```

- [ ] **Step 8: Write the spike report**

Create `docs/superpowers/specs/2026-06-23-phase0-spike-findings.md` recording, in prose:
- Whether `evdi_add_device` needed root, and the exact node used.
- Observed time from connect → `mode_changed`.
- Dirty-rect behaviour (how many rects per frame, whether full-frame on first grab).
- Any API signature deviations from this plan's assumptions (so Phase 1 uses the real ones).
- Measured/observed pixel format and stride.

```bash
git add docs/superpowers/specs/2026-06-23-phase0-spike-findings.md
git commit -m "docs: phase 0 evdi spike findings"
```

This report is the input that makes the Phase 1 streaming plan concrete instead of speculative.

---

## Self-Review

**1. Spec coverage (Phase 0 scope only):** The spec's Phase 0 is "load module, create a monitor via libevdi, confirm KWin extends onto it and framebuffer data arrives." Covered: Task 3 creates/connects the monitor (KWin extension verified via `kscreen-doctor`), Task 4 receives framebuffer + dirty rects, Task 5 proves frames reach userspace as PNGs. The `evdi`/`libevdi 1.14.16`/C++17/distrobox/immutable-host constraints from the spec's environment + decisions sections are encoded in Global Constraints. Phases 1–5 are intentionally out of scope for this plan (each gets its own plan after the spike).

**2. Placeholder scan:** No "TBD"/"implement later". The one deliberate stub (`edid.cpp` placeholder in Task 1) is created and then fully replaced in Task 2 — called out explicitly. Every code step contains complete code.

**3. Type consistency:** `Frame` is defined once (Task 4) and consumed with the same fields in Task 5 (`width/height/stride/bgra/rects/valid`). `VirtualDisplay::handle()` returns `evdi_handle`, consumed by `Capturer(evdi_handle)`. `build_edid`/`timing_1080p60` signatures match between Task 2, 3, and 5. `save_png_from_bgra(const std::string&, const Frame&)` defined and used consistently. Library target `droppix_core` accumulates sources across tasks without renaming.

**Known external-API caveat (by design):** Exact `libevdi` signatures (`evdi_request_update` return type, `evdi_grab_pixels` args, `EVDI_INVALID_HANDLE` spelling) are pinned to 1.14.16 but the **vendored header is the source of truth**; each relevant task notes "adjust to the header if it differs." This is appropriate for a spike whose purpose is to validate exactly these calls.
