# Audio Output to the Tablet — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stream a dedicated audio channel (a virtual `droppix-audio` sink) from the PC to the tablet, so an app's audio (e.g. a Wii U emulator's gamepad audio) plays on the tablet.

**Architecture:** A persistent PipeWire null-sink `droppix-audio` (owned by the GUI); the root streamer captures its monitor with `pw-record` run in the user session, frames raw PCM as a new `AUDIO` wire message on the existing TLS connection, funnelled through the single-threaded stream loop; the tablet plays it via `AudioTrack` on a dedicated thread.

**Tech Stack:** C++17 host + Qt6 GUI, PipeWire (`pw-record`, `pactl`), GoogleTest, the existing length-prefixed TLS protocol, Kotlin/Android `AudioTrack`, JUnit.

## Global Constraints

- **Fixed audio format everywhere:** 48000 Hz, signed 16-bit little-endian, 2 channels (stereo, interleaved). No per-message format header.
- **Best-effort:** any audio failure (missing `pw-record`/sink, capture error, `AudioTrack` failure) logs once and continues **video-only**; audio never stalls or breaks video/touch.
- **No new build dependencies:** capture via the `pw-record` CLI (already installed); raw PCM needs no codec; do not link libpipewire.
- **PipeWire runs in the user session:** the GUI is a user process and runs `pactl` directly; the root streamer spawns `pw-record` through the existing `user_cmd_prefix()` (as kscreen/touch-bind do).
- **Reuse the existing connection:** new `MsgType::Audio = 9` (host) / `MsgType.AUDIO(9)` (Kotlin); `Orientation = 8` is the current max.
- **Single-threaded TLS:** all socket writes stay on the stream-loop thread; audio reaches it via a queue drained each loop iteration.

## Build & test commands

- **Host configure (after adding/removing source files):**
  `distrobox enter droppix-dev -- bash -lc 'cmake -S "/var/mnt/nas/Projects/Spacedesk for linux/host" -B /home/Spinjitsudoomyt/droppix-build'`
- **Host build + all tests:**
  `distrobox enter droppix-dev -- bash -lc 'cmake --build /home/Spinjitsudoomyt/droppix-build -j && ctest --test-dir /home/Spinjitsudoomyt/droppix-build --output-on-failure'`
- **Host single gtest:** append e.g. `-R Audio` to the `ctest` call.
- **Android unit tests:**
  `distrobox enter droppix-android -- bash -lc 'export ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle; cd "/var/mnt/nas/Projects/Spacedesk for linux/android"; bash gradlew --no-daemon testDebugUnitTest'`
- **Android APK build:** same prefix, `bash gradlew --no-daemon assembleDebug`.

## File structure

| File | Responsibility |
|------|----------------|
| `host/src/protocol.h` (mod) | `MsgType::Audio = 9` |
| `android/…/protocol/Protocol.kt` (mod) | `MsgType.AUDIO(9)` |
| `host/src/audio_streamer.{h,cpp}` (new) | Capture PCM from `droppix-audio.monitor`, queue, `drain()` |
| `host/gui/audio_sink.{h,cpp}` (new) | Create/adopt/release the `droppix-audio` null-sink (injectable `pactl` runner) |
| `host/src/transport_server.{h,cpp}` (mod) | `send_audio(pcm)` |
| `host/src/stream_daemon.{h,cpp}` (mod) | `StreamConfig.audio`; construct `AudioStreamer`, drain+send each iteration |
| `host/src/stream_main.cpp` (mod) | `--audio` flag → `cfg.audio` |
| `host/gui/settings.h` (mod) | `bool audio` |
| `host/gui/args_builder.cpp` (mod) | append `--audio` |
| `host/gui/main_window.{h,cpp}` (mod) | "Stream audio to tablet" checkbox; own a `DroppixAudioSink` |
| `android/…/audio/AudioPlayer.kt` (new) | `AudioTrack` + queue + playback thread |
| `android/…/net/TransportClient.kt` (mod) | route `AUDIO` → `StreamListener.onAudio` |
| `android/…/ui/StreamActivity.kt` (mod) | own `AudioPlayer`, wire `onAudio` |

---

### Task 1: `AUDIO` message type + shared wire vector

**Files:**
- Modify: `host/src/protocol.h:9` (the `MsgType` enum)
- Modify: `android/app/src/main/java/com/droppix/app/protocol/Protocol.kt` (the `MsgType` enum)
- Test: `host/tests/test_protocol.cpp` (append), `android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt` (append)

**Interfaces:**
- Produces: `MsgType::Audio = 9` (C++), `MsgType.AUDIO` with code `9` (Kotlin). An `AUDIO` message body is raw interleaved s16le PCM; framed with the existing `encode_message` / `Protocol.encodeMessage`.

- [ ] **Step 1: Host — failing test.** Append to `host/tests/test_protocol.cpp`:

```cpp
TEST(Protocol, AudioMessageFraming) {
  // length = 1 (type) + 4 (body) = 5, big-endian; type = Audio(9).
  std::vector<unsigned char> pcm = {0xDE, 0xAD, 0xBE, 0xEF};
  auto m = droppix::encode_message(droppix::MsgType::Audio, pcm);
  std::vector<unsigned char> expected = {0,0,0,5, 9, 0xDE,0xAD,0xBE,0xEF};
  EXPECT_EQ(m, expected);
}
```

- [ ] **Step 2: Run it, expect FAIL** (no `MsgType::Audio`). Build command above; expect a compile error on `MsgType::Audio`.

- [ ] **Step 3: Host — implement.** In `host/src/protocol.h`, change the enum line to:

```cpp
enum class MsgType : uint8_t {
  Hello = 1, Config = 2, Video = 3, Ping = 4, Pong = 5, Bye = 6, Input = 7,
  Orientation = 8, Audio = 9
};
```

- [ ] **Step 4: Run host tests, expect PASS** (`ctest … -R Protocol`).

- [ ] **Step 5: Android — failing test.** Append to `ProtocolTest.kt`:

```kotlin
@Test fun audioMessageFramingMatchesHost() {
    val m = Protocol.encodeMessage(MsgType.AUDIO,
        byteArrayOf(0xDE.toByte(), 0xAD.toByte(), 0xBE.toByte(), 0xEF.toByte()))
    assertArrayEquals(
        byteArrayOf(0,0,0,5, 9, 0xDE.toByte(),0xAD.toByte(),0xBE.toByte(),0xEF.toByte()), m)
}
```

- [ ] **Step 6: Android — implement.** In `Protocol.kt`, extend the enum:

```kotlin
enum class MsgType(val code: Int) {
    HELLO(1), CONFIG(2), VIDEO(3), PING(4), PONG(5), BYE(6), INPUT(7), ORIENTATION(8), AUDIO(9);
    companion object {
        fun fromCode(c: Int): MsgType? = entries.firstOrNull { it.code == c }
    }
}
```

- [ ] **Step 7: Run Android unit tests, expect PASS** (`bash gradlew … testDebugUnitTest`).

- [ ] **Step 8: Commit.**

```bash
git add host/src/protocol.h host/tests/test_protocol.cpp \
  android/app/src/main/java/com/droppix/app/protocol/Protocol.kt \
  android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt
git commit -m "feat(proto): AUDIO message type (9), shared host<->Kotlin wire vector"
```

---

### Task 2: Host `AudioStreamer`

**Files:**
- Create: `host/src/audio_streamer.h`, `host/src/audio_streamer.cpp`
- Modify: `host/CMakeLists.txt` (add `src/audio_streamer.cpp` to `droppix_core` at line ~34; add `tests/test_audio_streamer.cpp` to `droppix_tests` at line ~102)
- Test: `host/tests/test_audio_streamer.cpp`

**Interfaces:**
- Produces: `class AudioStreamer { bool start(const std::string& user_prefix); void read_from_fd(int fd); bool drain(std::vector<std::vector<unsigned char>>& out); void stop(); ~AudioStreamer(); };`
  - `start()` spawns `pw-record … droppix-audio.monitor` and reads it; returns false if spawn fails.
  - `read_from_fd(fd)` begins reading an already-open fd (takes ownership) — used by `start()` and tests.
  - `drain(out)` appends all queued PCM chunks to `out`, clears the queue, returns whether anything was added. Non-blocking.

- [ ] **Step 1: Failing test.** Create `host/tests/test_audio_streamer.cpp`:

```cpp
#include <gtest/gtest.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include "audio_streamer.h"
using namespace droppix;

TEST(AudioStreamer, ReadsChunksFromFdAndDrains) {
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  AudioStreamer a;
  a.read_from_fd(fds[0]);            // read end; AudioStreamer owns it
  const unsigned char data[] = {1,2,3,4,5,6,7,8};
  ASSERT_EQ(write(fds[1], data, sizeof(data)), (ssize_t)sizeof(data));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<std::vector<unsigned char>> out;
  EXPECT_TRUE(a.drain(out));
  size_t total = 0; for (auto& c : out) total += c.size();
  EXPECT_EQ(total, sizeof(data));

  std::vector<std::vector<unsigned char>> empty;
  EXPECT_FALSE(a.drain(empty));      // queue cleared by the first drain
  close(fds[1]);
  a.stop();
}
```

- [ ] **Step 2: Add the test + source to `host/CMakeLists.txt`.** Add `src/audio_streamer.cpp` to the `droppix_core` source list (after `src/input_injector.cpp`), and `tests/test_audio_streamer.cpp` to the `droppix_tests` source list (after `tests/test_orientation.cpp`). Re-run the **host configure** command.

- [ ] **Step 3: Run it, expect FAIL** (no `audio_streamer.h`).

- [ ] **Step 4: Implement the header.** Create `host/src/audio_streamer.h`:

```cpp
#pragma once
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace droppix {

// Captures raw PCM (s16le/48000/stereo) from the droppix-audio sink's monitor
// via pw-record (run in the user session) and hands chunks to the stream loop.
// Best-effort: if capture cannot start, drain() simply yields nothing.
class AudioStreamer {
 public:
  ~AudioStreamer() { stop(); }

  // Spawn pw-record under `user_prefix` on droppix-audio.monitor and begin
  // reading. Returns false if the process could not be started.
  bool start(const std::string& user_prefix);

  // Begin reading PCM from an already-open fd (takes ownership of the fd).
  void read_from_fd(int fd);

  // Append all queued PCM chunks to `out`, clear the queue. Returns true if any
  // were added. Non-blocking; safe to call from the stream loop.
  bool drain(std::vector<std::vector<unsigned char>>& out);

  void stop();

 private:
  void begin_reading();
  void reader_loop();

  int fd_ = -1;
  FILE* proc_ = nullptr;             // set when started via popen()
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::mutex mu_;
  std::vector<std::vector<unsigned char>> queue_;   // guarded by mu_
};

}  // namespace droppix
```

- [ ] **Step 5: Implement the source.** Create `host/src/audio_streamer.cpp`:

```cpp
#include "audio_streamer.h"
#include <unistd.h>

namespace droppix {

bool AudioStreamer::start(const std::string& user_prefix) {
  const std::string cmd =
      user_prefix +
      "pw-record --raw --target=droppix-audio.monitor "
      "--format=s16 --rate=48000 --channels=2 --latency=20ms - 2>/dev/null";
  proc_ = ::popen(cmd.c_str(), "r");
  if (!proc_) return false;
  fd_ = ::fileno(proc_);
  begin_reading();
  return true;
}

void AudioStreamer::read_from_fd(int fd) {
  fd_ = fd;
  begin_reading();
}

void AudioStreamer::begin_reading() {
  running_ = true;
  thread_ = std::thread(&AudioStreamer::reader_loop, this);
}

void AudioStreamer::reader_loop() {
  unsigned char buf[4096];
  while (running_) {
    ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n <= 0) break;                       // EOF / error / pipe closed
    std::lock_guard<std::mutex> lk(mu_);
    queue_.emplace_back(buf, buf + n);
  }
}

bool AudioStreamer::drain(std::vector<std::vector<unsigned char>>& out) {
  std::lock_guard<std::mutex> lk(mu_);
  if (queue_.empty()) return false;
  for (auto& c : queue_) out.push_back(std::move(c));
  queue_.clear();
  return true;
}

void AudioStreamer::stop() {
  running_ = false;
  if (proc_) { ::pclose(proc_); proc_ = nullptr; fd_ = -1; }  // closes pipe -> reader read() returns
  else if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
  if (thread_.joinable()) thread_.join();
}

}  // namespace droppix
```

- [ ] **Step 6: Run it, expect PASS** (`ctest … -R AudioStreamer`).

- [ ] **Step 7: Commit.**

```bash
git add host/src/audio_streamer.h host/src/audio_streamer.cpp \
  host/tests/test_audio_streamer.cpp host/CMakeLists.txt
git commit -m "feat(host): AudioStreamer captures droppix-audio monitor PCM"
```

---

### Task 3: Host `DroppixAudioSink` (GUI)

**Files:**
- Create: `host/gui/audio_sink.h`, `host/gui/audio_sink.cpp`
- Modify: `host/CMakeLists.txt` (add `gui/audio_sink.cpp` to `droppix_gui` `target_sources` at line ~68; add `tests/test_audio_sink.cpp` **and** `gui/audio_sink.cpp` to `droppix_tests`, mirroring how `gui/args_builder.cpp` is already compiled into `droppix_tests` for `test_args_builder.cpp`)
- Test: `host/tests/test_audio_sink.cpp`

**Interfaces:**
- Produces: `class DroppixAudioSink { using Runner = std::function<std::pair<bool,std::string>(const std::string&)>; explicit DroppixAudioSink(Runner = default_runner()); ~DroppixAudioSink(); void ensure(); void release(); bool created_here() const; static Runner default_runner(); };`
  - `ensure()`: adopt an existing `droppix-audio` sink, else create one; idempotent.
  - `release()`: unload only a sink this instance created.

- [ ] **Step 1: Failing test.** Create `host/tests/test_audio_sink.cpp`:

```cpp
#include <gtest/gtest.h>
#include <vector>
#include "audio_sink.h"
using namespace droppix;

TEST(DroppixAudioSink, AdoptsExistingSinkWithoutCreating) {
  std::vector<std::string> calls;
  DroppixAudioSink::Runner fake = [&](const std::string& cmd) {
    calls.push_back(cmd);
    if (cmd.find("list short sinks") != std::string::npos)
      return std::make_pair(true, std::string("12\tdroppix-audio\tPipeWire\ts16le 2ch 48000Hz\tIDLE\n"));
    return std::make_pair(true, std::string());
  };
  DroppixAudioSink sink(fake);
  sink.ensure();
  EXPECT_FALSE(sink.created_here());
  sink.release();
  for (auto& c : calls) EXPECT_EQ(c.find("load-module"), std::string::npos);
  for (auto& c : calls) EXPECT_EQ(c.find("unload-module"), std::string::npos);
}

TEST(DroppixAudioSink, CreatesThenUnloadsOnlyWhatItCreated) {
  std::vector<std::string> calls;
  DroppixAudioSink::Runner fake = [&](const std::string& cmd) {
    calls.push_back(cmd);
    if (cmd.find("list short sinks") != std::string::npos)
      return std::make_pair(true, std::string());            // no existing sink
    if (cmd.find("load-module") != std::string::npos)
      return std::make_pair(true, std::string("42\n"));      // pactl prints module index
    return std::make_pair(true, std::string());
  };
  DroppixAudioSink sink(fake);
  sink.ensure();
  EXPECT_TRUE(sink.created_here());
  sink.release();
  bool unloaded42 = false;
  for (auto& c : calls) if (c.find("unload-module 42") != std::string::npos) unloaded42 = true;
  EXPECT_TRUE(unloaded42);
}
```

- [ ] **Step 2: Add sources to `host/CMakeLists.txt`** (see Files), re-run host configure.

- [ ] **Step 3: Run it, expect FAIL** (no `audio_sink.h`).

- [ ] **Step 4: Implement the header.** Create `host/gui/audio_sink.h`:

```cpp
#pragma once
#include <functional>
#include <string>
#include <utility>

namespace droppix {

// Owns the lifecycle of the 'droppix-audio' PipeWire null-sink in the user
// session. ensure() adopts an existing sink or creates one; release() unloads
// only a sink this instance created. pactl is invoked via an injectable runner.
class DroppixAudioSink {
 public:
  // Runs a shell command; returns {exit_ok, captured stdout}.
  using Runner = std::function<std::pair<bool, std::string>(const std::string&)>;

  explicit DroppixAudioSink(Runner runner = default_runner());
  ~DroppixAudioSink() { release(); }

  void ensure();                 // idempotent
  void release();                // unload only if created_here()
  bool created_here() const { return created_; }

  static Runner default_runner();   // popen-based, captures stdout

 private:
  Runner runner_;
  bool created_ = false;
  std::string module_index_;     // index from load-module, used by unload-module
};

}  // namespace droppix
```

- [ ] **Step 5: Implement the source.** Create `host/gui/audio_sink.cpp`:

```cpp
#include "audio_sink.h"
#include <array>
#include <cstdio>
#include <memory>

namespace droppix {
namespace {
std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  size_t b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
}  // namespace

DroppixAudioSink::Runner DroppixAudioSink::default_runner() {
  return [](const std::string& cmd) -> std::pair<bool, std::string> {
    std::unique_ptr<FILE, int (*)(FILE*)> p(::popen(cmd.c_str(), "r"), ::pclose);
    if (!p) return {false, std::string()};
    std::string out; std::array<char, 256> buf;
    while (std::fgets(buf.data(), (int)buf.size(), p.get())) out += buf.data();
    return {true, out};
  };
}

DroppixAudioSink::DroppixAudioSink(Runner runner) : runner_(std::move(runner)) {}

void DroppixAudioSink::ensure() {
  if (created_) return;
  auto [ok, sinks] = runner_("pactl list short sinks 2>/dev/null");
  if (ok && sinks.find("droppix-audio") != std::string::npos) {
    created_ = false;            // adopt an existing sink; don't own it
    return;
  }
  auto [lok, idx] = runner_(
      "pactl load-module module-null-sink sink_name=droppix-audio "
      "sink_properties=device.description=droppix-audio 2>/dev/null");
  std::string id = trim(idx);
  if (lok && !id.empty()) { module_index_ = id; created_ = true; }
}

void DroppixAudioSink::release() {
  if (!created_ || module_index_.empty()) return;
  runner_("pactl unload-module " + module_index_ + " 2>/dev/null");
  created_ = false;
  module_index_.clear();
}

}  // namespace droppix
```

- [ ] **Step 6: Run it, expect PASS** (`ctest … -R DroppixAudioSink`).

- [ ] **Step 7: Commit.**

```bash
git add host/gui/audio_sink.h host/gui/audio_sink.cpp \
  host/tests/test_audio_sink.cpp host/CMakeLists.txt
git commit -m "feat(host): DroppixAudioSink manages the droppix-audio null-sink"
```

---

### Task 4: Wire audio into the streamer (transport + config + loop)

**Files:**
- Modify: `host/src/transport_server.h` (declare `send_audio`), `host/src/transport_server.cpp` (define it)
- Modify: `host/src/stream_daemon.h:10-20` (add `bool audio`), `host/src/stream_daemon.cpp` (construct `AudioStreamer`, drain+send)
- Modify: `host/src/stream_main.cpp` (`--audio` flag + `cfg` init)

**Interfaces:**
- Consumes: `AudioStreamer` (Task 2), `MsgType::Audio` (Task 1).
- Produces: `bool TransportServer::send_audio(const std::vector<unsigned char>& pcm);`; `StreamConfig.audio`.

- [ ] **Step 1: Transport — declare + define `send_audio`.** In `host/src/transport_server.h`, after the `send_video(...)` declaration add:

```cpp
  bool send_audio(const std::vector<unsigned char>& pcm);
```

In `host/src/transport_server.cpp`, after `send_video`'s definition add:

```cpp
bool TransportServer::send_audio(const std::vector<unsigned char>& pcm) {
  return send_all(encode_message(MsgType::Audio, pcm));
}
```

- [ ] **Step 2: Config — add the field.** In `host/src/stream_daemon.h`, add as the **last** field of `StreamConfig` (after `ApprovalGate* gate = nullptr;`):

```cpp
  bool audio = false;          // capture droppix-audio monitor and stream it
```

- [ ] **Step 3: Daemon — capture + send.** In `host/src/stream_daemon.cpp`, add the include near the top (with the other project includes):

```cpp
#include "audio_streamer.h"
```

After the touch/`InputInjector` block (just before `auto t0 = …;`), construct the streamer:

```cpp
  // Audio capture (opt-in via --audio): capture the droppix-audio sink monitor in
  // the user session and stream it. Best-effort; never blocks the video path.
  AudioStreamer audio;
  if (cfg_.audio) {
    if (audio.start(user_cmd_prefix()))
      std::fprintf(stderr, "audio: capturing droppix-audio.monitor\n");
    else
      std::fprintf(stderr, "audio: capture unavailable (pw-record/droppix-audio missing)\n");
  }
```

Change the frame-timeout line to also tick tightly for audio:

```cpp
  const int frame_timeout = (cfg_.touch || cfg_.audio) ? 8 : 1000;
```

As the **first** statement inside the `while (!stop && !restart_for_orientation && tx_.connected()) {` loop body, drain + send audio:

```cpp
    if (cfg_.audio) {
      std::vector<std::vector<unsigned char>> chunks;
      if (audio.drain(chunks))
        for (auto& c : chunks) tx_.send_audio(c);
    }
```

- [ ] **Step 4: stream_main — flag + cfg.** In `host/src/stream_main.cpp`, add a flag local near the other `bool` locals:

```cpp
  bool audio = false;
```

Add a parse arm in the arg loop (after the `--tls`/`--cert`/`--key` arms):

```cpp
    else if (a == "--audio") audio = true;
```

Append `audio` as the **last** element of the `StreamConfig` aggregate initializer passed to `StreamDaemon`:

```cpp
    droppix::StreamDaemon daemon(src, enc, tx,
        {fps, bitrate, stats_json, touch, droppix::Rect{mx, my, mw, mh}, dtw, dth,
         orientation, &g_orientation, approve, &g_gate, audio});
```

- [ ] **Step 5: Build + run all host tests, expect PASS** (no regressions; new code compiles). Use the host build+test command.

- [ ] **Step 6: Commit.**

```bash
git add host/src/transport_server.h host/src/transport_server.cpp \
  host/src/stream_daemon.h host/src/stream_daemon.cpp host/src/stream_main.cpp
git commit -m "feat(host): stream droppix-audio PCM as AUDIO messages when --audio"
```

---

### Task 5: GUI toggle + sink ownership

**Files:**
- Modify: `host/gui/settings.h` (add `bool audio`)
- Modify: `host/gui/args_builder.cpp` (append `--audio`)
- Modify: `host/gui/main_window.h` (checkbox + `DroppixAudioSink` member), `host/gui/main_window.cpp` (create/wire the checkbox; `ensure()` the sink)
- Test: `host/tests/test_args_builder.cpp` (append)

**Interfaces:**
- Consumes: `DroppixAudioSink` (Task 3), `Settings.audio`.
- Produces: `--audio` appears in the built command iff `settings.audio`.

- [ ] **Step 1: Settings — add field.** In `host/gui/settings.h`, after `bool tls = true;` (or alongside `touch`):

```cpp
  bool audio = false;   // capture droppix-audio sink and stream it to the tablet
```

- [ ] **Step 2: args_builder — failing test.** Append to `host/tests/test_args_builder.cpp`:

```cpp
TEST(ArgsBuilder, AudioFlagAppendedWhenEnabled) {
  droppix::Settings s; s.source = droppix::Settings::Source::Evdi; s.audio = true;
  auto c = droppix::build_command(s, "/usr/bin/droppix_stream");
  bool has_audio = false;
  for (auto& a : c.args) if (a == "--audio") has_audio = true;
  EXPECT_TRUE(has_audio);

  droppix::Settings s2; s2.source = droppix::Settings::Source::Evdi; s2.audio = false;
  auto c2 = droppix::build_command(s2, "/usr/bin/droppix_stream");
  for (auto& a : c2.args) EXPECT_NE(a, std::string("--audio"));
}
```

- [ ] **Step 3: Run it, expect FAIL.**

- [ ] **Step 4: args_builder — implement.** In `host/gui/args_builder.cpp`, after the `a.push_back("--stats-json");` line add:

```cpp
  if (s.audio) a.push_back("--audio");
```

- [ ] **Step 5: Run it, expect PASS** (`ctest … -R ArgsBuilder`).

- [ ] **Step 6: GUI — checkbox + sink.** In `host/gui/main_window.h`, add the include:

```cpp
#include "audio_sink.h"
```

Add the widget pointer near `QCheckBox* touch_;`:

```cpp
  QCheckBox* audio_;
```

Add the sink as a member near the other members (e.g. after `CertManager cert_;`):

```cpp
  DroppixAudioSink audioSink_;
```

In `host/gui/main_window.cpp`, create the checkbox right after the `touch_` checkbox is created (near line 68) and add it to the form after `form->addRow("", touch_);`:

```cpp
  audio_ = new QCheckBox("Stream audio to tablet (route an app's output to 'droppix-audio')");
```
```cpp
  form->addRow("", audio_);
```

In `collectSettings()` (near `s.touch = touch_->isChecked();`):

```cpp
  s.audio = audio_->isChecked();
```

In `applySettings()` (near `touch_->setChecked(s.touch);`):

```cpp
  audio_->setChecked(s.audio);
```

At the **end** of the `MainWindow` constructor body, ensure the sink exists:

```cpp
  audioSink_.ensure();   // create/adopt the droppix-audio sink for this session
```

(The `audioSink_` member's destructor releases it — RAII; no explicit teardown needed.)

- [ ] **Step 7: Build the GUI, expect success.** Run the host build command (it builds `droppix_gui` when Qt6 is present). Expect `droppix_gui` links.

- [ ] **Step 8: Commit.**

```bash
git add host/gui/settings.h host/gui/args_builder.cpp host/tests/test_args_builder.cpp \
  host/gui/main_window.h host/gui/main_window.cpp
git commit -m "feat(gui): 'Stream audio to tablet' toggle + droppix-audio sink lifecycle"
```

---

### Task 6: Tablet playback

**Files:**
- Create: `android/app/src/main/java/com/droppix/app/audio/AudioPlayer.kt`
- Modify: `android/app/src/main/java/com/droppix/app/net/TransportClient.kt` (`StreamListener.onAudio`, route `AUDIO`)
- Modify: `android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt` (own `AudioPlayer`, implement `onAudio`)
- Test: `android/app/src/test/java/com/droppix/app/net/TransportClientTest.kt` (append)

**Interfaces:**
- Consumes: `MsgType.AUDIO` (Task 1).
- Produces: `StreamListener.onAudio(pcm: ByteArray)` (default empty body); `class AudioPlayer { fun start(); fun submit(pcm: ByteArray); fun release() }`.

- [ ] **Step 1: Failing test.** Append to `TransportClientTest.kt` a test that the client routes an `AUDIO` frame to `onAudio` (mirror `handshakeReceivesConfigAndVideo`: the fake server sends one `AUDIO` message after CONFIG; assert the bytes arrive):

```kotlin
@Test fun routesAudioToListener() {
    val server = testServerSocketFactory().createServerSocket(0) as SSLServerSocket
    val port = server.localPort
    val pcm = byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8)
    thread {
        server.use {
            val sock = it.accept() as javax.net.ssl.SSLSocket
            sock.startHandshake()
            val input = DataInputStream(sock.getInputStream())
            val len = input.readInt(); val frame = ByteArray(len); input.readFully(frame)  // HELLO
            val out = sock.getOutputStream()
            out.write(Protocol.encodeMessage(MsgType.AUDIO, pcm)); out.flush()
            Thread.sleep(200); sock.close()
        }
    }
    val latch = CountDownLatch(1)
    var got: ByteArray? = null
    val listener = object : StreamListener {
        override fun onConfig(config: Protocol.Config) {}
        override fun onVideo(video: Protocol.Video) {}
        override fun onAudio(pcm: ByteArray) { got = pcm; latch.countDown() }
    }
    val client = TransportClient()
    val tlsTrust = TlsTrust(FakePinStore())
    val t = thread { client.run("127.0.0.1", port, 1920, 1080, 320, listener, { true }, tlsTrust = tlsTrust) }
    assertTrue(latch.await(3, TimeUnit.SECONDS))
    assertArrayEquals(pcm, got)
    // stop the client run loop
    Thread.sleep(50)
}
```

- [ ] **Step 2: Run it, expect FAIL** (no `onAudio`).

- [ ] **Step 3: Add `onAudio` to `StreamListener` + route it.** In `TransportClient.kt`, extend the interface with a defaulted method (so existing implementors don't break):

```kotlin
interface StreamListener {
    fun onConfig(config: Protocol.Config)
    fun onVideo(video: Protocol.Video)
    fun onAudio(pcm: ByteArray) {}
}
```

In the `when (msg.type)` block in `run()`, add an arm:

```kotlin
                            MsgType.AUDIO -> listener.onAudio(msg.body)
```

- [ ] **Step 4: Run the test, expect PASS** (`bash gradlew … testDebugUnitTest`).

- [ ] **Step 5: Implement `AudioPlayer`.** Create `android/app/src/main/java/com/droppix/app/audio/AudioPlayer.kt`:

```kotlin
package com.droppix.app.audio

import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.util.Log
import java.util.concurrent.LinkedBlockingQueue

// Plays raw s16le/48k/stereo PCM via AudioTrack on a dedicated thread. The net
// thread only submit()s; playback (a blocking write) never stalls the net loop.
class AudioPlayer {
    companion object { private const val RATE = 48000; private const val TAG = "droppix" }

    private val queue = LinkedBlockingQueue<ByteArray>(64)   // bounded -> latency stays low
    @Volatile private var running = false
    private var thread: Thread? = null
    private var track: AudioTrack? = null

    fun start() {
        if (running) return
        val min = AudioTrack.getMinBufferSize(RATE,
            AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT)
        val bufSize = if (min > 0) min * 2 else 8192
        track = try {
            AudioTrack(AudioManager.STREAM_MUSIC, RATE, AudioFormat.CHANNEL_OUT_STEREO,
                AudioFormat.ENCODING_PCM_16BIT, bufSize, AudioTrack.MODE_STREAM)
        } catch (e: Exception) { Log.w(TAG, "AudioTrack init failed: ${e.message}"); null }
        val t = track ?: return
        t.play()
        running = true
        thread = Thread({ loop(t) }, "droppix-audio").apply { isDaemon = true; start() }
    }

    fun submit(pcm: ByteArray) {
        if (!running) return
        if (!queue.offer(pcm)) { queue.poll(); queue.offer(pcm) }   // drop oldest on overflow
    }

    private fun loop(t: AudioTrack) {
        while (running) {
            val pcm = try { queue.take() } catch (e: InterruptedException) { break }
            try { t.write(pcm, 0, pcm.size) } catch (e: Exception) { break }
        }
    }

    fun release() {
        running = false
        thread?.interrupt(); thread?.join(500); thread = null
        try { track?.stop(); track?.release() } catch (_: Exception) {}
        track = null; queue.clear()
    }
}
```

- [ ] **Step 6: Wire `AudioPlayer` into `StreamActivity`.** In `StreamActivity.kt`, add the import:

```kotlin
import com.droppix.app.audio.AudioPlayer
```

Add a field near `client`:

```kotlin
    @Volatile private var audioPlayer: AudioPlayer? = null
```

In `startStreaming()`, after `client = c` (inside the net thread), create and start the player, and add `onAudio` to the listener object:

```kotlin
            val player = AudioPlayer().apply { start() }
            audioPlayer = player
```
```kotlin
                override fun onAudio(pcm: ByteArray) { player.submit(pcm) }
```

After the `while (running)` loop ends (near `client = null`), release it:

```kotlin
            player.release(); audioPlayer = null
```

- [ ] **Step 7: Build the APK, expect success** (`bash gradlew … assembleDebug`).

- [ ] **Step 8: Commit.**

```bash
git add android/app/src/main/java/com/droppix/app/audio/AudioPlayer.kt \
  android/app/src/main/java/com/droppix/app/net/TransportClient.kt \
  android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt \
  android/app/src/test/java/com/droppix/app/net/TransportClientTest.kt
git commit -m "feat(android): play streamed AUDIO via AudioTrack on a dedicated thread"
```

---

## Manual verification (after all tasks)

1. Launch the GUI; confirm `droppix-audio` appears: `pactl list short sinks | grep droppix-audio`.
2. Enable **"Stream audio to tablet"**, Start, connect the tablet.
3. In `pavucontrol` (Playback tab), route an app (e.g. a media player or Cemu's gamepad output) to **droppix-audio**.
4. Confirm that audio plays **on the tablet** while other apps' audio stays on the PC speakers.
5. Toggle audio off / disconnect / reconnect — video is unaffected throughout; audio resumes when re-enabled.

## Notes for the implementer

- `pw-record`'s `-` (stdout) + `--raw` emits headerless PCM; if a specific `pw-record` build rejects `-`, fall back to `parec --device=droppix-audio.monitor --rate=48000 --channels=2 --format=s16le` (also raw to stdout) — same `AudioStreamer` reader.
- The stream loop already runs as the single TLS writer; do not add audio sends from any other thread.
- Keep audio strictly best-effort: every new failure path logs and returns; none may throw out of the video path.
