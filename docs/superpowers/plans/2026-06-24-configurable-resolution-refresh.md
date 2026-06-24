# Configurable Resolution + Refresh (evdi) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the evdi virtual monitor's resolution and refresh rate configurable (currently fixed 1080p60) via a CVT timing module, `droppix_stream` flags, and host-GUI dropdowns.

**Architecture:** A new pure `cvt` module computes a VESA CVT reduced-blanking `Timing` for any width×height×refresh; the existing `build_edid` turns it into the EDID. `EvdiFrameSource` builds its EDID from the chosen mode (using the verified `timing_1080p60()` CEA preset for the exact 1080p60 default, CVT otherwise). The host GUI gets resolution (incl. 16:10) + refresh dropdowns. No Android or wire-protocol change — CONFIG already carries the encode dimensions.

**Tech Stack:** C++17, the existing `Timing`/`build_edid`/`EvdiFrameSource`/`droppix_stream`, Qt6 GUI, GoogleTest.

## Global Constraints

- **Build/test env:** distrobox `droppix-dev`, off the CIFS mount: `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B /home/Spinjitsudoomyt/droppix-build && cmake --build /home/Spinjitsudoomyt/droppix-build -j && ctest --test-dir /home/Spinjitsudoomyt/droppix-build --output-on-failure'`. GUI build/run: see prior host-GUI plan (Qt6, run on host).
- **C++17**, namespace `droppix`. Engine under `host/src`, GUI under `host/gui`.
- **Reuse** `Timing` + `build_edid` + `timing_1080p60()` (host/src/edid.h) unchanged. CVT only *produces* `Timing` values.
- **CVT-RB reference values (verified against the `cvt -r` algorithm — assert these exactly):**
  - 1920×1080@60 → clk 138500 kHz, v_sync 5, v_blank 31  (h: active 1920, front 48, sync 32, blank 160)
  - 1280×720@60  → clk 64000 kHz, v_sync 5, v_blank 21
  - 2560×1600@60 → clk 268500 kHz, v_sync 6, v_blank 46
  - 1920×1200@60 → clk 154000 kHz, v_sync 6, v_blank 35
  - 1920×1080@30 → clk 68250 kHz, v_sync 5, v_blank 16
  - (all reduced-blanking: h_front 48, h_sync 32, h_blank 160, v_front 3)
- **Default mode is 1920×1080@60 via the CEA `timing_1080p60()` preset** (clk 148500), NOT CVT — the common path stays on the known-good timing. CVT is for everything else.
- **Refresh (EDID) and capture FPS are separate.** `--refresh` sets EDID refresh; `--fps` stays the capture rate.
- **evdi source only.** No protocol/Android change.

---

## File Structure

```
host/src/
  cvt.h  cvt.cpp            # CVT-RB timing + mode_timing(default-aware) (pure)
  evdi_frame_source.h .cpp  # MODIFY: take (w,h,refresh), build EDID from mode_timing
  stream_main.cpp           # MODIFY: --width/--height apply to evdi; add --refresh
host/tests/
  test_cvt.cpp
host/gui/
  settings.h                # MODIFY: add refresh_hz
  args_builder.cpp          # MODIFY: evdi emits --width/--height/--refresh
  profile_store.cpp         # MODIFY: persist refresh_hz
  main_window.cpp           # MODIFY: enable resolution for evdi; add refresh dropdown; presets
host/tests/test_args_builder.cpp    # MODIFY: evdi now includes width/height/refresh
host/gui/tests/test_profile_store.cpp  # MODIFY: refresh round-trips
```

---

### Task 1: CVT timing module (TDD, exact reference values)

**Files:**
- Create: `host/src/cvt.h`, `host/src/cvt.cpp`, `host/tests/test_cvt.cpp`
- Modify: `host/CMakeLists.txt` (add `src/cvt.cpp` to `droppix_core`, `tests/test_cvt.cpp` to `droppix_tests`)

**Interfaces:**
- Produces:
  - `droppix::Timing droppix::cvt_rb_timing(int width, int height, int refresh_hz);` — CVT reduced-blanking timing.
  - `droppix::Timing droppix::mode_timing(int width, int height, int refresh_hz);` — returns `timing_1080p60()` for exactly (1920,1080,60), else `cvt_rb_timing(...)`.

- [ ] **Step 1: Write the failing tests**

`host/tests/test_cvt.cpp`:

```cpp
#include <gtest/gtest.h>
#include "cvt.h"
#include "edid.h"

using namespace droppix;

static void expectRb(const Timing& t, int clk, int vsync, int vblank,
                     int w, int h) {
  EXPECT_EQ(t.pixel_clock_khz, clk);
  EXPECT_EQ(t.h_active, (w / 8) * 8);
  EXPECT_EQ(t.h_front, 48); EXPECT_EQ(t.h_sync, 32); EXPECT_EQ(t.h_blank, 160);
  EXPECT_EQ(t.v_active, h);
  EXPECT_EQ(t.v_front, 3); EXPECT_EQ(t.v_sync, vsync); EXPECT_EQ(t.v_blank, vblank);
}

TEST(Cvt, Rb1080p60)  { expectRb(cvt_rb_timing(1920,1080,60), 138500, 5, 31, 1920,1080); }
TEST(Cvt, Rb720p60)   { expectRb(cvt_rb_timing(1280, 720,60),  64000, 5, 21, 1280, 720); }
TEST(Cvt, Rb1600p60)  { expectRb(cvt_rb_timing(2560,1600,60), 268500, 6, 46, 2560,1600); }
TEST(Cvt, Rb1200p60)  { expectRb(cvt_rb_timing(1920,1200,60), 154000, 6, 35, 1920,1200); }
TEST(Cvt, Rb1080p30)  { expectRb(cvt_rb_timing(1920,1080,30),  68250, 5, 16, 1920,1080); }

TEST(Cvt, ModeTimingUsesCeaPresetFor1080p60) {
  // The default 1080p60 must use the verified CEA preset (148500), not CVT (138500).
  EXPECT_EQ(mode_timing(1920,1080,60).pixel_clock_khz, 148500);
}
TEST(Cvt, ModeTimingUsesCvtForOtherModes) {
  EXPECT_EQ(mode_timing(1280,720,60).pixel_clock_khz, 64000);
}
TEST(Cvt, BuildEdidOfCvtEncodesActivePixels) {
  auto e = build_edid(cvt_rb_timing(2560,1600,60));
  ASSERT_EQ(e.size(), 128u);
  const int o = 54;  // DTD #1
  int h_active = e[o+2] | ((e[o+4] & 0xF0) << 4);
  int v_active = e[o+5] | ((e[o+7] & 0xF0) << 4);
  EXPECT_EQ(h_active, 2560);
  EXPECT_EQ(v_active, 1600);
}
```

- [ ] **Step 2: Add to CMake, build, verify failure** (`cvt.h` not found).

- [ ] **Step 3: Write the header**

`host/src/cvt.h`:

```cpp
#pragma once
#include "edid.h"   // droppix::Timing

namespace droppix {
// VESA CVT reduced-blanking v1 timing for width x height @ refresh_hz.
// h_active is rounded down to the 8-px cell granularity.
Timing cvt_rb_timing(int width, int height, int refresh_hz);

// The EDID timing to advertise for a mode: the verified CEA preset for the
// 1920x1080@60 default, CVT reduced-blanking otherwise.
Timing mode_timing(int width, int height, int refresh_hz);
}  // namespace droppix
```

- [ ] **Step 4: Write the implementation**

`host/src/cvt.cpp` (this exact algorithm was validated to reproduce `cvt -r`):

```cpp
#include "cvt.h"
#include <cmath>

namespace droppix {
namespace {
constexpr int kCell = 8;
constexpr double kClockStepKhz = 250.0;     // 0.25 MHz
constexpr double kRbMinVBlankUs = 460.0;
constexpr int kRbHBlank = 160, kRbHFront = 48, kRbHSync = 32, kRbVFront = 3;

int vsync_for(int w, int h) {
  const double ar = static_cast<double>(w) / static_cast<double>(h);
  const struct { double ar; int vs; } table[] = {
    {4.0/3, 4}, {16.0/9, 5}, {16.0/10, 6}, {5.0/4, 7}, {15.0/9, 7}};
  for (const auto& e : table) if (std::abs(ar - e.ar) < 0.02) return e.vs;
  return 10;
}
}  // namespace

Timing cvt_rb_timing(int width, int height, int refresh_hz) {
  const int h_active = (width / kCell) * kCell;
  const int vs = vsync_for(width, height);
  const double hperiod_us =
      (1e6 / refresh_hz - kRbMinVBlankUs) / (height + kRbVFront);
  int vbi = static_cast<int>(std::ceil(kRbMinVBlankUs / hperiod_us));
  const int vbi_min = kRbVFront + vs + 1;
  if (vbi < vbi_min) vbi = vbi_min;
  const int v_total = height + vbi;
  const int h_total = h_active + kRbHBlank;
  const double clk_khz = static_cast<double>(h_total) * v_total * refresh_hz / 1000.0;
  const int clk = static_cast<int>(std::floor(clk_khz / kClockStepKhz) * kClockStepKhz);

  Timing t{};
  t.pixel_clock_khz = clk;
  t.h_active = h_active; t.h_front = kRbHFront; t.h_sync = kRbHSync; t.h_blank = kRbHBlank;
  t.v_active = height;   t.v_front = kRbVFront; t.v_sync = vs;       t.v_blank = vbi;
  t.h_mm = static_cast<int>(std::lround(width * 25.4 / 96.0));   // ~96 DPI physical size
  t.v_mm = static_cast<int>(std::lround(height * 25.4 / 96.0));
  return t;
}

Timing mode_timing(int width, int height, int refresh_hz) {
  if (width == 1920 && height == 1080 && refresh_hz == 60) return timing_1080p60();
  return cvt_rb_timing(width, height, refresh_hz);
}
}  // namespace droppix
```

- [ ] **Step 5: Build + test → all `Cvt.*` pass** (and prior tests). Standard build+ctest.

- [ ] **Step 6: Commit**

```bash
git add host/src/cvt.h host/src/cvt.cpp host/tests/test_cvt.cpp host/CMakeLists.txt
git commit -m "feat(cvt): CVT reduced-blanking timing + default-aware mode_timing"
```

---

### Task 2: evdi source takes the mode + streamer flags

**Files:**
- Modify: `host/src/evdi_frame_source.h`, `host/src/evdi_frame_source.cpp`
- Modify: `host/src/stream_main.cpp`

**Interfaces:**
- Consumes: `mode_timing` (Task 1), `build_edid`.
- Produces: `EvdiFrameSource(int width, int height, int refresh_hz)` whose `start()` connects evdi with `build_edid(mode_timing(width,height,refresh_hz))`.

- [ ] **Step 1: Update the EvdiFrameSource header**

`host/src/evdi_frame_source.h` — add ctor + members:

```cpp
#pragma once
#include <memory>
#include "frame_source.h"
#include "virtual_display.h"
#include "capturer.h"

namespace droppix {
class EvdiFrameSource : public FrameSource {
 public:
  EvdiFrameSource(int width, int height, int refresh_hz)
      : width_(width), height_(height), refresh_hz_(refresh_hz) {}
  bool start(int& width, int& height) override;
  Frame next(int timeout_ms) override;
 private:
  int width_, height_, refresh_hz_;
  VirtualDisplay display_;
  std::unique_ptr<Capturer> cap_;
};
}  // namespace droppix
```

- [ ] **Step 2: Update the implementation**

`host/src/evdi_frame_source.cpp` — use `mode_timing`:

```cpp
#include "evdi_frame_source.h"
#include "edid.h"
#include "cvt.h"
#include <cstdio>

namespace droppix {

bool EvdiFrameSource::start(int& width, int& height) {
  if (!display_.open()) return false;
  display_.connect(build_edid(mode_timing(width_, height_, refresh_hz_)));
  cap_ = std::make_unique<Capturer>(display_.handle());
  if (!cap_->wait_for_mode(5000)) {
    std::fprintf(stderr, "evdi: no KWin mode within 5s for %dx%d@%d\n",
                 width_, height_, refresh_hz_);
    cap_.reset();
    display_.disconnect();
    return false;
  }
  width = cap_->width();
  height = cap_->height();
  return true;
}

Frame EvdiFrameSource::next(int timeout_ms) {
  if (!cap_) return Frame{};
  return cap_->grab(timeout_ms);
}
}  // namespace droppix
```

- [ ] **Step 3: Update stream_main flags + construction**

In `host/src/stream_main.cpp`:
- Change the default dimensions and add a refresh local: where flag locals are declared, set `int width = 1920, height = 1080, refresh = 60;` (default mode is 1080p60; the GUI/e2e always pass explicit values).
- In the arg loop add: `else if (a == "--refresh") refresh = val();`
- Change the evdi source construction from `droppix::EvdiFrameSource evdi;` to `droppix::EvdiFrameSource evdi(width, height, refresh);`.
- The test-pattern source keeps using `width`/`height` as before (now defaulting to 1920×1080 instead of 1280×720 — harmless; all tests/GUI pass explicit `--width/--height`).

- [ ] **Step 4: Build → links; prior tests still pass**

Standard build+ctest. Expected: clean build (engine + GUI), all prior tests pass. (The live evdi run at a new mode is operator-verified in Task 3.)

- [ ] **Step 5: Commit**

```bash
git add host/src/evdi_frame_source.h host/src/evdi_frame_source.cpp host/src/stream_main.cpp
git commit -m "feat(evdi): configurable resolution+refresh via mode_timing and --refresh"
```

---

### Task 3: GUI — refresh setting, evdi resolution enabled, dropdowns

**Files:**
- Modify: `host/gui/settings.h` (add `refresh_hz`)
- Modify: `host/gui/args_builder.cpp` (evdi emits `--width/--height/--refresh`)
- Modify: `host/gui/profile_store.cpp` (persist `refresh_hz`)
- Modify: `host/gui/main_window.cpp` (enable resolution for evdi; refresh dropdown; presets)
- Modify: `host/tests/test_args_builder.cpp`, `host/gui/tests/test_profile_store.cpp`

**Interfaces:**
- Consumes: `Settings` (gains `refresh_hz`), `build_command`.
- Produces: GUI with resolution (enabled for evdi) + refresh dropdowns; `build_command` emits `--refresh` (and `--width/--height`) for the evdi source.

- [ ] **Step 1: Add `refresh_hz` to Settings**

`host/gui/settings.h` — add the field:

```cpp
  int fps = 30, bitrate_kbps = 8000, port = 27000;
  int refresh_hz = 60;
  bool auto_adb_reverse = true;
```

- [ ] **Step 2: Update the ArgsBuilder test (TDD) then args_builder**

In `host/tests/test_args_builder.cpp`, replace the `EvdiUsesPkexecAndNoTestPattern` test so it asserts evdi now includes width/height/refresh:

```cpp
TEST(ArgsBuilder, EvdiUsesPkexecWithModeFlags) {
  Settings s; s.source = Settings::Source::Evdi;
  s.width = 2560; s.height = 1600; s.refresh_hz = 60;
  Command c = build_command(s, "/path/droppix_stream");
  EXPECT_EQ(c.program, "pkexec");
  EXPECT_EQ(c.args.front(), "/path/droppix_stream");
  EXPECT_FALSE(has(c.args, "--test-pattern"));
  EXPECT_TRUE(has(c.args, "--width"));  EXPECT_TRUE(has(c.args, "2560"));
  EXPECT_TRUE(has(c.args, "--height")); EXPECT_TRUE(has(c.args, "1600"));
  EXPECT_TRUE(has(c.args, "--refresh")); EXPECT_TRUE(has(c.args, "60"));
  EXPECT_TRUE(has(c.args, "--stats-json"));
}
```

Run tests → this fails (evdi currently omits width/height/refresh). Then update `host/gui/args_builder.cpp` `build_command` so the source block adds the mode flags for BOTH sources, and `--refresh` for evdi:

```cpp
Command build_command(const Settings& s, const std::string& stream_bin) {
  std::vector<std::string> a;
  if (s.source == Settings::Source::TestPattern) {
    a.push_back("--test-pattern");
  }
  // Both sources take the dimensions; evdi additionally advertises a refresh.
  a.push_back("--width");  a.push_back(std::to_string(s.width));
  a.push_back("--height"); a.push_back(std::to_string(s.height));
  if (s.source == Settings::Source::Evdi) {
    a.push_back("--refresh"); a.push_back(std::to_string(s.refresh_hz));
  }
  a.push_back("--fps");     a.push_back(std::to_string(s.fps));
  a.push_back("--bitrate"); a.push_back(std::to_string(s.bitrate_kbps));
  a.push_back("--port");    a.push_back(std::to_string(s.port));
  a.push_back("--stats-json");

  Command c;
  c.needs_adb_reverse = s.auto_adb_reverse;
  if (s.source == Settings::Source::Evdi) {
    c.program = "pkexec";
    c.args.push_back(stream_bin);
    c.args.insert(c.args.end(), a.begin(), a.end());
  } else {
    c.program = stream_bin;
    c.args = a;
  }
  return c;
}
```

(Note: the existing test-pattern test `TestPatternRunsBinaryDirectly` still passes — it already expects `--width/--height`. The old `EvdiUsesPkexecAndNoTestPattern` is replaced by the new one above.)

- [ ] **Step 3: Persist refresh in ProfileStore (TDD)**

In `host/gui/tests/test_profile_store.cpp`, extend the round-trip test to set + assert `refresh_hz` (e.g. add `s.refresh_hz = 30;` before save and `EXPECT_EQ(out.refresh_hz, 30);` after load). Run → fails. Then in `host/gui/profile_store.cpp`, add to `toJson`: `o["refresh_hz"] = s.refresh_hz;` and to `fromJson`: `s.refresh_hz = o["refresh_hz"].toInt(s.refresh_hz);`.

- [ ] **Step 4: Build + test → ArgsBuilder + ProfileStore tests pass** (and all prior). Standard build+ctest.

- [ ] **Step 5: Update MainWindow — enable resolution for evdi, add refresh, presets**

In `host/gui/main_window.cpp`:
- Expand the resolution preset list:
```cpp
  resolution_->addItems({"1280x720", "1920x1080", "2560x1440",
                         "1280x800", "1920x1200", "2560x1600"});
  resolution_->setCurrentText("1920x1080");
```
- **Remove the evdi-disable** of the resolution control: delete the `syncResEnabled` lambda + its `connect` + the initial call (resolution is now valid for both sources).
- Add a refresh dropdown next to FPS:
```cpp
  refresh_ = new QComboBox; refresh_->addItems({"30", "60"}); refresh_->setCurrentText("60");
  // ... in the form layout, after Resolution or FPS:
  form->addRow("Refresh (Hz):", refresh_);
```
  (Declare `QComboBox* refresh_;` as a member in `main_window.h`.)
- In `collectSettings()` add: `s.refresh_hz = refresh_->currentText().toInt();`
- In `applySettings()` add: `refresh_->setCurrentText(QString::number(s.refresh_hz));`

- [ ] **Step 6: Build → droppix_gui links; all tests pass**

Standard build+ctest. Expected: clean build; the resolution combo is enabled for evdi; the refresh dropdown is present. (No new UI unit test — Qt UI; behavior is operator-verified next.)

- [ ] **Step 7: Commit**

```bash
git add host/gui/settings.h host/gui/args_builder.cpp host/gui/profile_store.cpp host/gui/main_window.h host/gui/main_window.cpp host/tests/test_args_builder.cpp host/gui/tests/test_profile_store.cpp
git commit -m "feat(gui): resolution+refresh dropdowns for evdi"
```

- [ ] **Step 8: Operator live check.** Run `/home/Spinjitsudoomyt/droppix-build/droppix_gui`. Select *Real monitor (evdi)*, pick a non-default mode (e.g. **2560×1600 @ 60**, the Nexus 10's native 16:10), Start → `pkexec` → confirm KWin sets that mode on the droppix monitor (System Settings → Display shows 2560×1600) and the tablet fills its screen with no letterboxing. Try a couple modes (1920×1200, 1280×720) and a 30 Hz refresh. Record results in `docs/superpowers/specs/2026-06-24-resolution-refresh-findings.md`.

```bash
git add docs/superpowers/specs/2026-06-24-resolution-refresh-findings.md
git commit -m "docs: resolution/refresh operator findings"
```

---

## Self-Review

**1. Spec coverage:** CVT module + `mode_timing` default bypass (Task 1); evdi source takes width/height/refresh + `--refresh` flag, width/height now apply to evdi (Task 2); GUI resolution enabled for evdi + refresh dropdown + 16:10 presets + `Settings.refresh_hz` + `ArgsBuilder` + `ProfileStore` (Task 3); no protocol/Android change (CONFIG carries dims). Refresh-vs-fps separation preserved (`--refresh` distinct from `--fps`). Default stays 1080p60 CEA preset.

**2. Placeholder scan:** No TBD/TODO. CVT algorithm is complete and pre-validated against `cvt -r`; all test expected values are concrete verified numbers. The findings doc (Task 3 Step 8) is filled from the operator run.

**3. Type consistency:** `cvt_rb_timing`/`mode_timing` return the existing `Timing` (edid.h), consumed by `build_edid` and `EvdiFrameSource`. `EvdiFrameSource(int,int,int)` ctor matches the `stream_main` construction. `Settings.refresh_hz` defined in Task 3 Step 1, consumed by `ArgsBuilder`, `ProfileStore`, `MainWindow`. `build_command` argv (evdi now with `--width/--height/--refresh`) matches the flags `stream_main` parses (`--width/--height/--refresh`). `mode_timing(1920,1080,60)` → CEA 148500; `cvt_rb_timing(1920,1080,60)` → 138500 — distinct and both asserted. The `has()` helper used in the new ArgsBuilder test already exists in `test_args_builder.cpp`.

**Verified-by-construction note:** the CVT-RB algorithm in Task 1 was run and confirmed to reproduce the known `cvt -r` outputs for 1080p60 (138500/2080/1111) and 720p60 (64000/1440/741) before being written into this plan, so the hard-coded test expectations are trustworthy.
