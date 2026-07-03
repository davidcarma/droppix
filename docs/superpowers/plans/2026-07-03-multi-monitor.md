# Multi-monitor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Let several tablets connect at once, each an independent extended monitor at its own native resolution, managed by the GUI.

**Architecture:** Each `droppix_stream` stays self-contained (own port, evdi monitor, touch device). Two streamer changes — build the evdi source *after* HELLO at the tablet's native size, and a unique `--touch-name` — plus a GUI that manages a list of sessions instead of one.

**Tech Stack:** C++17 host (evdi/uinput/x264/OpenSSL), Qt6 GUI, GoogleTest. Build in the `droppix-dev` distrobox off-mount: `distrobox enter droppix-dev -- bash -lc 'cmake --build /home/Spinjitsudoomyt/droppix-build -j && ctest --test-dir /home/Spinjitsudoomyt/droppix-build'`.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-03-multi-monitor-design.md`.
- Native per-tablet resolution (evdi created after HELLO); shared fps/bitrate/touch/overlay/tls.
- Unique touch device name per session: `droppix-touch-<port>`.
- Cap ~4 monitors. Audio single-session. mDNS idle-advert stays single base-port.
- Preserve single-session behaviour + the existing orientation-restart mechanism.
- Commit style: branch merges; `git -c user.name="Claude" -c user.email="noreply@anthropic.com"`.

---

### Task 1: Streamer — unique touch device name

**Files:** Modify `host/src/input_injector.{h,cpp}` (open takes a name), `host/src/stream_daemon.{h,cpp}` (StreamConfig.touch_name; bind_touch_to_output takes the name; pass name to open+bind), `host/src/stream_main.cpp` (`--touch-name` flag → StreamConfig).

**Interfaces produced:** `InputInjector::open(const std::string& name)`; `StreamConfig.touch_name` (default `"droppix-touch"`); `bind_touch_to_output(std::string output_name, std::string touch_name)`.

- [ ] `InputInjector::open(const std::string& name = "droppix-touch")` — use `name` in `us.name` instead of the literal.
- [ ] `StreamConfig` gains `std::string touch_name = "droppix-touch";`.
- [ ] `bind_touch_to_output(output_name, touch_name)` — replace the literal `droppix-touch` in the shell with the arg; update the detached-thread call `std::thread(bind_touch_to_output, droppix.name, cfg_.touch_name)`.
- [ ] `stream_daemon` calls `injector.open(cfg_.touch_name)`.
- [ ] `stream_main`: parse `--touch-name <s>` into the config (add to the cfg initializer).
- [ ] Build + `ctest` (all pass, single-session unaffected). Commit.

---

### Task 2: Streamer — source factory + native-resolution (evdi after HELLO)

**Files:** Modify `host/src/stream_daemon.{h,cpp}`, `host/src/stream_main.cpp`.

**Interfaces produced:** `StreamDaemon(std::function<std::unique_ptr<FrameSource>(int w,int h)> make_source, Encoder&, TransportServer&, StreamConfig)`.

- [ ] Change `StreamDaemon` to store `std::function<std::unique_ptr<FrameSource>(int,int)> make_source_;` + `std::unique_ptr<FrameSource> src_;` instead of `FrameSource& src_`.
- [ ] In `run_until`, reorder: snapshot `before_outputs` → `accept_client` → `read_hello(cver,cw,ch,…)` → approve gate → compute `bool portrait = (orient==1||orient==3); int sw = portrait?ch:cw, sh = portrait?cw:ch;` (fallback to `cfg_.monitor`/`--width/height` when `cw==0||ch==0`) → `src_ = make_source_(sw, sh); int w=sw,h=sh; if(!src_->start(w,h)) return false;` → `enc_.open` → `send_config` → identify output / orientation / touch / stream loop (unchanged, using `src_->next`).
- [ ] `stream_main`: build the factory once — `auto make = [&](int w,int h)->std::unique_ptr<FrameSource>{ return test_pattern ? std::unique_ptr<FrameSource>(new TestPatternSource(w,h,fps)) : std::unique_ptr<FrameSource>(new EvdiFrameSource(w,h,refresh)); };` and pass to `StreamDaemon`. Remove the per-iteration source construction; keep the reconnect loop + orientation restart (the daemon rebuilds via the factory each `run_until`).
- [ ] Build + `ctest`; run the test-pattern e2e still works: `droppix_stream --test-pattern --frames 30 --port 27055` (a client connecting drives dims). Commit.

---

### Task 3: GUI — pure port allocator

**Files:** Create `host/gui/port_alloc.{h,cpp}`, `host/gui/tests/test_port_alloc.cpp`; register in `host/CMakeLists.txt` (droppix_gui sources + droppix_gui_tests).

**Interfaces produced:** `int droppix::allocate_port(int base, const std::set<int>& used, int cap=4)` — lowest `base+k` (0≤k<cap) not in `used`; `-1` if full.

- [ ] Failing test `test_port_alloc.cpp`:
```cpp
#include "port_alloc.h"
#include <gtest/gtest.h>
using namespace droppix;
TEST(PortAlloc, FirstIsBase){ EXPECT_EQ(allocate_port(27000, {}), 27000); }
TEST(PortAlloc, SkipsUsed){ EXPECT_EQ(allocate_port(27000, {27000,27001}), 27002); }
TEST(PortAlloc, FillsHoles){ EXPECT_EQ(allocate_port(27000, {27000,27002}), 27001); }
TEST(PortAlloc, FullReturnsNeg1){ EXPECT_EQ(allocate_port(27000, {27000,27001,27002,27003}, 4), -1); }
```
- [ ] Implement `allocate_port` (loop k=0..cap-1, return base+k if not in used).
- [ ] Register + build gui_tests + run. Commit.

---

### Task 4: GUI — SessionManager

**Files:** Create `host/gui/session_manager.{h,cpp}`, `host/gui/tests/test_session_manager.cpp`; register in CMake.

**Interfaces produced:**
```cpp
struct Session { StreamController* controller; int port; QString touchName; QString key; QString label; QString transport; };
class SessionManager {
 public:
  bool has(const QString& key) const;       // a live session for this client key
  int  allocatePort(int base) const;        // uses port_alloc over live ports
  Session& add(const QString& key, StreamController* c, int port, QString touchName, QString label, QString transport);
  void remove(const QString& key);
  QList<Session>& list();
  std::set<int> usedPorts() const;
  int count() const;
};
```
- [ ] Failing test: add two sessions (mock StreamController=nullptr ok for bookkeeping), `has`/`count`/`usedPorts`/`allocatePort` skip used, `remove` drops one. (Construct StreamController-less Sessions by passing nullptr; SessionManager must not deref the controller in bookkeeping methods.)
- [ ] Implement SessionManager (QList<Session>, allocatePort delegates to `allocate_port(base, usedPorts())`).
- [ ] Register + build + run. Commit.

---

### Task 5: GUI — multi-session integration (MainWindow)

**Files:** Modify `host/gui/main_window.{h,cpp}`, `host/gui/args_builder.{h,cpp}` (build_command gains a per-session port + touch-name).

**Interfaces consumed:** SessionManager, allocate_port, StreamController.

Sub-steps:
- [ ] `args_builder`: `build_command(const Settings&, const std::string& stream_bin, int port, const std::string& touch_name)` — override the port arg with `port`, add `--touch-name <name>` when evdi. Keep a default overload (port=s.port, name="droppix-touch") so existing tests compile; update args_builder tests to cover the new params.
- [ ] MainWindow: replace `StreamController controller_;` with `SessionManager sessions_;` + a factory that `new StreamController(this)` per session. Add `wireSession(Session&)` that connects that controller's `logLine`/`statsReceived`/`runningChanged`/`connecting`/`approvalRequested` with the session captured (pairing popup + approve dialog act per session; on `runningChanged(false)` remove the session + its Active-monitors row).
- [ ] `onConnectToSelectedDevice`: if `sessions_.has(key)` return; else `port = sessions_.allocatePort(settings.port)` (guard -1 → "monitor limit reached"); `c = new StreamController(this)`; `wireSession`; `c->start(build_command(collectSettings(), streamBin_, port, ("droppix-touch-"+QString::number(port)).toStdString()))`; direct the tablet to `port` (network → `encode_wake(port)` UDP; USB → `adb_.usbConnect(serial, port)`); `sessions_.add(...)`; add an Active-monitors row.
- [ ] Add an **"Active monitors"** `QGroupBox` with a `QListWidget` (one row per session: "label · transport · :port") + a **Stop** button (stops the selected session's controller). Rows added on connect, removed on `runningChanged(false)`.
- [ ] "Start streaming" button → start a **default-port** session (port = allocatePort(base), touch-name derived) for the no-client-selected USB/localhost case; becomes "Stop all" style only if you want (keep simple: it just starts one session).
- [ ] Audio: pass `--audio` (`s.audio`) only to the FIRST session (track `bool audioTaken_`); later sessions get audio off.
- [ ] `closeEvent`: stop every session's controller.
- [ ] Build gui; screenshot the Active-monitors panel. Commit.

---

### Task 6: Build, tests, artifacts

- [ ] Full `ctest` green; `droppix_gui` builds; launch offscreen smoke test (no crash).
- [ ] Rebuild AppImage (`bash packaging/appimage/build-appimage.sh`) + refresh `~/droppix-preview/droppix.AppImage`.
- [ ] Merge to master, push. On-device (needs a 2nd tablet): connect two tablets → two native-res monitors; touch on each lands on its own; Stop one leaves the other.

---

## Self-Review

- **Spec coverage:** A (Task 2) ✓, B (Task 1) ✓, C (Tasks 3-5) ✓, D — reused per-session in Task 5 ✓, E — native-res in Task 2 + audio-single in Task 5 ✓.
- **Placeholders:** none — each task has concrete files/interfaces/tests.
- **Type consistency:** `allocate_port`/`SessionManager::allocatePort` (Task 3/4) consumed in Task 5; `build_command(...,port,touch_name)` defined in Task 5's args_builder step and used in the connect step; `touch_name` (Task 1) flows GUI→streamer.
