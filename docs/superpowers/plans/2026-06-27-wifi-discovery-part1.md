# WiFi + Discovery + Client GUI — Part 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect the tablet to the PC over WiFi with a real Android Connect screen
(discovered PCs via mDNS + manual IP + reconnect-to-last) and an approve-on-host gate.

**Architecture:** Connection stays tablet→PC (PC already listens on `INADDR_ANY`). The
tablet's `HELLO` carries a name+id; the streamer gates non-localhost peers by emitting an
`approve-request` on stderr and awaiting an `approve/deny` line on its (already-existing)
stdin control channel, which the host GUI drives (with a remembered-id store + dialog).
The Android app splits into `ConnectActivity` (new launcher, Material dark theme, NSD
browse) and `StreamActivity` (the existing fullscreen surface, parameterized by host:port).

**Tech Stack:** C++17 (host, GoogleTest, CMake), Qt6 Widgets (host GUI), Kotlin/Android
(minSdk 21, Material Components, NSD), Avahi CLI (`avahi-publish-service`).

**Spec:** `docs/superpowers/specs/2026-06-27-wifi-discovery-client-gui-design.md`

## Global Constraints

- Host C++ builds in the `droppix-dev` distrobox; build dir OFF the CIFS mount at
  `/home/Spinjitsudoomyt/droppix-build` (`cmake --build` + `ctest`).
- Android builds in the `droppix-android` distrobox: `ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk`,
  run `bash gradlew --no-daemon` from `android/`; build dir is off-mount via `build.gradle.kts`.
- minSdk 21 (Nexus 10 / API 22). No Jetpack Compose — Material Components Views only.
- Wire integers are big-endian; framed message = `[u32 BE len][u8 type][body]`, `len`
  covers the type byte. Host enum + Kotlin enum must stay byte-identical (assert in tests).
- `git merge` on this mount intermittently errors `fatal: stash failed`; run each merge as
  its own standalone command with `--no-autostash` (never chain `checkout && merge`).
- Commit messages end with: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

## File Structure

- `host/src/protocol.{h,cpp}` — extend `encode_hello`/`decode_hello` to v3 (name+id).
- `host/src/approval.h` (new, header-only, pure) — parse `approve`/`deny` lines; `ApprovalGate`.
- `host/src/transport_server.{h,cpp}` — expose the connected peer's IP string.
- `host/src/stream_daemon.{h,cpp}` — non-localhost approval handshake before CONFIG.
- `host/src/stream_main.cpp` — `--approve` flag; stdin reader also feeds the `ApprovalGate`.
- `host/gui/approved_store.{h,cpp}` (new) — persist approved ids (one per line).
- `host/gui/stream_controller.{h,cpp}` — `writeLine()` to streamer stdin; `approvalRequested` signal.
- `host/gui/mdns_advertiser.{h,cpp}` (new) — start/stop `avahi-publish-service` QProcess.
- `host/gui/main_window.{h,cpp}` — wire advertiser + approval dialog.
- `android/.../protocol/Protocol.kt` — `encodeHello` v3 (name+id).
- `android/.../net/DeviceIdentity.kt` (new) — stable id + display name.
- `android/.../net/Discovery.kt` (new) — NSD browse of `_droppix._tcp`.
- `android/.../ui/ConnectActivity.kt` (new launcher) + `res/layout/activity_connect.xml`.
- `android/.../ui/MainActivity.kt` → rename to `StreamActivity.kt` (host/port extras).
- `android/.../net/TransportClient.kt` — send name+id in HELLO.
- `android/.../res/values/themes.xml`, `AndroidManifest.xml` — Material dark theme, launcher, permissions.

---

### Task 1: HELLO v3 carries device name + id (host)

**Files:**
- Modify: `host/src/protocol.cpp` (`encode_hello`, `decode_hello`), `host/src/protocol.h`
- Test: `host/tests/test_protocol.cpp`

**Interfaces:**
- Produces: `std::vector<unsigned char> encode_hello(uint32_t version, uint32_t w, uint32_t h, uint32_t density, const std::string& name, const std::string& id);`
- Produces: `bool decode_hello(const std::vector<unsigned char>& b, uint32_t& version, uint32_t& w, uint32_t& h, uint32_t& d, std::string& name, std::string& id);` — a 16-byte body yields empty name/id (back-compat).
- `kProtocolVersion` becomes `3`.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(Protocol, HelloV3RoundTrip) {
  auto body = encode_hello(3, 1920, 1080, 320, "Nexus 10", "abc123");
  uint32_t v,w,h,d; std::string name,id;
  ASSERT_TRUE(decode_hello(body, v,w,h,d, name,id));
  EXPECT_EQ(v,3u); EXPECT_EQ(w,1920u); EXPECT_EQ(h,1080u); EXPECT_EQ(d,320u);
  EXPECT_EQ(name,"Nexus 10"); EXPECT_EQ(id,"abc123");
}
TEST(Protocol, HelloV2BackCompatNoNameId) {
  // old 16-byte HELLO (version 2) -> name/id default empty, still decodes.
  std::vector<unsigned char> b;
  // reuse the v3 encoder but truncate to the first 16 bytes
  auto full = encode_hello(2, 1280, 720, 160, "x", "y");
  b.assign(full.begin(), full.begin()+16);
  uint32_t v,w,h,d; std::string name,id;
  ASSERT_TRUE(decode_hello(b, v,w,h,d, name,id));
  EXPECT_EQ(v,2u); EXPECT_EQ(w,1280u); EXPECT_TRUE(name.empty()); EXPECT_TRUE(id.empty());
}
```

- [ ] **Step 2: Run, verify fail** — `ctest --test-dir /home/Spinjitsudoomyt/droppix-build -R Hello` → FAIL (signature mismatch / won't compile).

- [ ] **Step 3: Implement.** In `protocol.h` replace the `encode_hello`/`decode_hello`
declarations with the v3 signatures above and `constexpr uint32_t kProtocolVersion = 3;`.
In `protocol.cpp`:

```cpp
std::vector<unsigned char> encode_hello(uint32_t version, uint32_t w, uint32_t h,
                                        uint32_t d, const std::string& name, const std::string& id) {
  std::vector<unsigned char> b;
  put_u32(b, version); put_u32(b, w); put_u32(b, h); put_u32(b, d);
  put_u16(b, (uint16_t)name.size()); b.insert(b.end(), name.begin(), name.end());
  put_u16(b, (uint16_t)id.size());   b.insert(b.end(), id.begin(),   id.end());
  return b;
}
bool decode_hello(const std::vector<unsigned char>& b, uint32_t& version,
                  uint32_t& w, uint32_t& h, uint32_t& d, std::string& name, std::string& id) {
  if (b.size() < 16) return false;
  version = get_u32(b.data()); w = get_u32(b.data()+4);
  h = get_u32(b.data()+8); d = get_u32(b.data()+12);
  name.clear(); id.clear();
  size_t p = 16;
  if (b.size() >= p+2) { uint16_t n = get_u16(b.data()+p); p += 2;
    if (b.size() >= p+n) { name.assign(b.begin()+p, b.begin()+p+n); p += n; } else return true; }
  if (b.size() >= p+2) { uint16_t n = get_u16(b.data()+p); p += 2;
    if (b.size() >= p+n) { id.assign(b.begin()+p, b.begin()+p+n); } }
  return true;
}
```

Update the existing `HelloRoundTrip` test and any caller (`transport_server.cpp`
`read_hello`, `stream_main`/tests) to the new signature — `read_hello` gains
`std::string& name, std::string& id` out-params; pass-through.

- [ ] **Step 4: Run, verify pass** — `ctest ... -R Hello` → PASS; full `ctest` green.

- [ ] **Step 5: Commit** — `git add host/src/protocol.* host/src/transport_server.* host/tests/test_protocol.cpp && git commit` (`feat(proto): HELLO v3 carries device name+id`).

---

### Task 2: HELLO v3 in the Android client + stable device identity

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/protocol/Protocol.kt` (`encodeHello`)
- Create: `android/app/src/main/java/com/droppix/app/net/DeviceIdentity.kt`
- Test: `android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt`

**Interfaces:**
- Produces: `Protocol.encodeHello(version, width, height, density, name: String, id: String): ByteArray`
- Produces: `DeviceIdentity.displayName(ctx): String` (e.g. `Build.MODEL`), `DeviceIdentity.stableId(ctx): String` (random UUID persisted in SharedPreferences).

- [ ] **Step 1: Write failing test** (byte-match with the host encoder):

```kotlin
@Test fun encodeHelloV3MatchesHostWireFormat() {
  // version=3,w=1,h=2,density=3, name="ab", id="cd"
  val b = Protocol.encodeHello(3, 1, 2, 3, "ab", "cd")
  // 4xu32 + u16 len + "ab" + u16 len + "cd"
  assertArrayEquals(byteArrayOf(0,0,0,3, 0,0,0,1, 0,0,0,2, 0,0,0,3,
                                0,2, 'a'.code.toByte(),'b'.code.toByte(),
                                0,2, 'c'.code.toByte(),'d'.code.toByte()), b)
}
```

- [ ] **Step 2: Run, verify fail** — `bash gradlew --no-daemon :app:testDebugUnitTest` → FAIL.

- [ ] **Step 3: Implement.** In `Protocol.kt` replace `encodeHello`:

```kotlin
fun encodeHello(version: Int, width: Int, height: Int, density: Int,
                name: String, id: String): ByteArray {
    val out = ArrayList<Byte>()
    putU32(out, version); putU32(out, width); putU32(out, height); putU32(out, density)
    val n = name.toByteArray(Charsets.UTF_8); val i = id.toByteArray(Charsets.UTF_8)
    out.add((n.size ushr 8).toByte()); out.add(n.size.toByte()); for (x in n) out.add(x)
    out.add((i.size ushr 8).toByte()); out.add(i.size.toByte()); for (x in i) out.add(x)
    return out.toByteArray()
}
```

Create `DeviceIdentity.kt`:

```kotlin
package com.droppix.app.net
import android.content.Context
import android.os.Build
import java.util.UUID
object DeviceIdentity {
    fun displayName(ctx: Context): String = Build.MODEL ?: "Android"
    fun stableId(ctx: Context): String {
        val p = ctx.getSharedPreferences("droppix", Context.MODE_PRIVATE)
        return p.getString("device_id", null) ?: UUID.randomUUID().toString().also {
            p.edit().putString("device_id", it).apply()
        }
    }
}
```

- [ ] **Step 4: Run, verify pass** — `:app:testDebugUnitTest` → PASS.

- [ ] **Step 5: Commit** — `feat(android): HELLO v3 name+id + stable DeviceIdentity`.

---

### Task 3: Approval gate (host, pure) + peer IP

**Files:**
- Create: `host/src/approval.h` (header-only)
- Modify: `host/src/transport_server.h`, `host/src/transport_server.cpp` (`peer_ip()`), `host/CMakeLists.txt` (add `tests/test_approval.cpp`)
- Test: `host/tests/test_approval.cpp`

**Interfaces:**
- Produces: `bool parse_approval(const std::string& line, std::string& id, bool& allow);` — `"approve X"`→(X,true), `"deny X"`→(X,false), else false.
- Produces: `class ApprovalGate { void submit(const std::string& id, bool allow); bool wait(const std::string& id, int timeout_ms, bool& allow); };` (mutex + condition_variable).
- Produces: `std::string TransportServer::peer_ip() const;` (e.g. `"127.0.0.1"` or `"192.168.x.y"`).

- [ ] **Step 1: Write failing tests**

```cpp
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
```

- [ ] **Step 2: Run, verify fail** — reconfigure CMake then `ctest -R Approval` → FAIL.

- [ ] **Step 3: Implement `approval.h`:**

```cpp
#pragma once
#include <string>
#include <map>
#include <mutex>
#include <condition_variable>
#include <chrono>
namespace droppix {
inline bool parse_approval(const std::string& line, std::string& id, bool& allow) {
  auto sp = line.find(' ');
  if (sp == std::string::npos) return false;
  std::string cmd = line.substr(0, sp);
  id = line.substr(sp + 1);
  while (!id.empty() && (id.back()=='\n'||id.back()=='\r'||id.back()==' ')) id.pop_back();
  if (cmd == "approve") { allow = true;  return !id.empty(); }
  if (cmd == "deny")    { allow = false; return !id.empty(); }
  return false;
}
class ApprovalGate {
 public:
  void submit(const std::string& id, bool allow) {
    { std::lock_guard<std::mutex> lk(m_); decisions_[id] = allow; }
    cv_.notify_all();
  }
  bool wait(const std::string& id, int timeout_ms, bool& allow) {
    std::unique_lock<std::mutex> lk(m_);
    if (!cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                      [&]{ return decisions_.count(id) > 0; })) return false;
    allow = decisions_[id]; decisions_.erase(id); return true;
  }
 private:
  std::mutex m_; std::condition_variable cv_; std::map<std::string,bool> decisions_;
};
}  // namespace droppix
```

Add `peer_ip()` to `TransportServer`: store the client address in `accept_client`
(after `accept`, `inet_ntop(AF_INET, &cli.sin_addr, buf, sizeof buf)` into a member
`std::string peer_ip_`), and `std::string peer_ip() const { return peer_ip_; }`.

- [ ] **Step 4: Run, verify pass** — `ctest -R Approval` → PASS; full `ctest` green.

- [ ] **Step 5: Commit** — `feat(host): approval gate + peer IP`.

---

### Task 4: Streamer gates non-localhost peers before CONFIG

**Files:**
- Modify: `host/src/stream_daemon.h` (`StreamConfig.approve`, `ApprovalGate* gate`), `host/src/stream_daemon.cpp`, `host/src/stream_main.cpp`

**Interfaces:**
- Consumes: `TransportServer::peer_ip()`, `decode_hello(... name, id)`, `ApprovalGate`.
- `StreamConfig` gains `bool approve = false;` and `ApprovalGate* gate = nullptr;`.

- [ ] **Step 1.** In `stream_main.cpp`: add `else if (a == "--approve") approve = true;`; create a
file-scope `static droppix::ApprovalGate g_gate;`; extend the stdin reader thread so each
**line** is parsed: if `parse_approval(line, id, allow)` → `g_gate.submit(id, allow)`, else
ignore; EOF still sets `g_stop`. (Switch the reader from `read 1 char` to line-buffered:
accumulate into a string, split on `\n`.) Pass `approve` and `&g_gate` into `StreamConfig`.

- [ ] **Step 2.** In `stream_daemon.cpp` `run_until`, after `read_hello(... name, id)` and
before `enc_.open`/`send_config`, insert the gate:

```cpp
if (cfg_.approve && cfg_.gate && tx_.peer_ip() != "127.0.0.1") {
  std::fprintf(stderr, "approve-request id=%s name=%s ip=%s\n",
               id.c_str(), name.c_str(), tx_.peer_ip().c_str());
  bool allow = false;
  if (!cfg_.gate->wait(id.empty() ? tx_.peer_ip() : id, 60000, allow) || !allow) {
    std::fprintf(stderr, "connection from %s denied\n", tx_.peer_ip().c_str());
    return false;   // closes the socket; reconnect loop continues
  }
}
```

- [ ] **Step 3: Manual verification (no unit test — needs a socket + stdin).** Build, then:

```bash
# terminal A: run the streamer in --approve mode reading approvals from a FIFO
mkfifo /tmp/dpx_in; /home/Spinjitsudoomyt/droppix-build/droppix_stream --test-pattern --approve --port 27997 < /tmp/dpx_in &
exec 9> /tmp/dpx_in
# terminal B: connect a fake client from a non-localhost view is hard; instead confirm
# localhost is AUTO-allowed (no approve-request) by connecting the real app over adb,
# and confirm a non-loopback connection logs "approve-request ...". Then: echo "approve <id>" >&9
```
Expected: localhost connection streams with NO `approve-request`; a LAN connection logs
`approve-request id=… name=… ip=…` and only proceeds after `approve <id>` is written.

- [ ] **Step 4: Commit** — `feat(host): approve-on-host gate for non-localhost peers`.

---

### Task 5: Host GUI — advertise via Avahi while streaming

**Files:**
- Create: `host/gui/mdns_advertiser.h`, `host/gui/mdns_advertiser.cpp`
- Modify: `host/gui/main_window.{h,cpp}` (own an `MdnsAdvertiser`; start/stop with the stream), `host/CMakeLists.txt` (add the .cpp to `droppix_gui`)

**Interfaces:**
- Produces: `class MdnsAdvertiser { void start(quint16 port); void stop(); bool available() const; };`
  — runs `avahi-publish-service "<hostname> (droppix)" _droppix._tcp <port>` as a `QProcess`;
  `available()` is `!QStandardPaths::findExecutable("avahi-publish-service").isEmpty()`.

- [ ] **Step 1: Implement `mdns_advertiser.h`:**

```cpp
#pragma once
#include <QObject>
#include <QProcess>
namespace droppix {
class MdnsAdvertiser : public QObject {
  Q_OBJECT
 public:
  explicit MdnsAdvertiser(QObject* p=nullptr) : QObject(p) {}
  bool available() const;
  void start(quint16 port);
  void stop();
 private:
  QProcess proc_;
};
}  // namespace droppix
```

`mdns_advertiser.cpp`:

```cpp
#include "mdns_advertiser.h"
#include <QStandardPaths>
#include <QHostInfo>
namespace droppix {
bool MdnsAdvertiser::available() const {
  return !QStandardPaths::findExecutable("avahi-publish-service").isEmpty();
}
void MdnsAdvertiser::start(quint16 port) {
  if (!available() || proc_.state() != QProcess::NotRunning) return;
  const QString name = QHostInfo::localHostName() + " (droppix)";
  proc_.start("avahi-publish-service",
              {name, "_droppix._tcp", QString::number(port)});
}
void MdnsAdvertiser::stop() {
  if (proc_.state() != QProcess::NotRunning) { proc_.terminate();
    if (!proc_.waitForFinished(1500)) proc_.kill(); }
}
}  // namespace droppix
```

- [ ] **Step 2: Wire into MainWindow.** Add `MdnsAdvertiser advertiser_;` member. In the
`runningChanged` handler: `if (r) advertiser_.start(collectSettings().port); else advertiser_.stop();`
Add `advertiser_.stop();` to `closeEvent`. Add `mdns_advertiser.cpp` to the `droppix_gui`
sources in `host/CMakeLists.txt`.

- [ ] **Step 3: Build + manual check** — `cmake --build …`; run the GUI, Start streaming, then
`avahi-browse -rtp _droppix._tcp` in a terminal should list `… (droppix)` with the port.
Stop → it disappears.

- [ ] **Step 4: Commit** — `feat(gui): advertise _droppix._tcp via avahi while streaming`.

---

### Task 6: Host GUI — approval dialog + remembered ids

**Files:**
- Create: `host/gui/approved_store.h`, `host/gui/approved_store.cpp`
- Test: `host/gui/tests/test_approved_store.cpp` (add to `droppix_gui_tests` in CMake)
- Modify: `host/gui/stream_controller.{h,cpp}` (`writeLine`, `approvalRequested` signal), `host/gui/main_window.{h,cpp}`

**Interfaces:**
- Produces: `class ApprovedStore { explicit ApprovedStore(QString dir); bool isApproved(const QString& id) const; void approve(const QString& id); void clear(); };` — persists to `<dir>/approved` (one id per line).
- Produces: `StreamController::writeLine(const QString&)` (writes `line+"\n"` to `proc_` stdin); signal `void approvalRequested(QString id, QString name, QString ip);` emitted when an `approve-request` log line is parsed in `onReadyRead`.

- [ ] **Step 1: Write failing test for `ApprovedStore`:**

```cpp
#include "approved_store.h"
#include <QTemporaryDir>
TEST(ApprovedStore, PersistsAcrossInstances) {
  QTemporaryDir d;
  { droppix::ApprovedStore s(d.path()); EXPECT_FALSE(s.isApproved("a")); s.approve("a"); }
  droppix::ApprovedStore s2(d.path());
  EXPECT_TRUE(s2.isApproved("a")); EXPECT_FALSE(s2.isApproved("b"));
  s2.clear(); EXPECT_FALSE(s2.isApproved("a"));
}
```

- [ ] **Step 2: Run, verify fail** — `ctest -R ApprovedStore` (after adding to CMake) → FAIL.

- [ ] **Step 3: Implement `approved_store.{h,cpp}`** (QFile read/write of `<dir>/approved`,
a `QSet<QString>` cache; `approve` appends + caches; `clear` empties the set + truncates).
Add `tests/test_approved_store.cpp` and `approved_store.cpp` to `droppix_gui_tests` and
`droppix_gui` in `host/CMakeLists.txt`.

- [ ] **Step 4: Run, verify pass** — `ctest -R ApprovedStore` → PASS.

- [ ] **Step 5: StreamController channel.** In `onReadyRead`, for each line, if it starts
with `"approve-request "`, parse `id=`, `name=`, `ip=` (split on spaces / `key=value`) and
`emit approvalRequested(id, name, ip)` instead of treating it as a normal log line. Add:

```cpp
void StreamController::writeLine(const QString& s) {
  if (proc_.state() == QProcess::Running) { proc_.write((s + "\n").toUtf8()); }
}
```

- [ ] **Step 6: MainWindow wiring.** Own an `ApprovedStore approved_{configDir()};`. Connect:

```cpp
connect(&controller_, &StreamController::approvalRequested, this,
  [this](const QString& id, const QString& name, const QString& ip){
    const QString key = id.isEmpty() ? ip : id;
    if (approved_.isApproved(key)) { controller_.writeLine("approve " + key); return; }
    auto btn = QMessageBox::question(this, "Allow connection?",
        QString("Allow \"%1\" (%2) to connect?").arg(name.isEmpty()?ip:name, ip));
    if (btn == QMessageBox::Yes) { approved_.approve(key); controller_.writeLine("approve " + key); }
    else controller_.writeLine("deny " + key);
  });
```

`build_command` must add `--approve` for the evdi (pkexec) path — edit
`host/gui/args_builder.cpp` to `a.push_back("--approve");` inside the Evdi branch.

- [ ] **Step 7: Build + manual check** — connect a LAN client → dialog appears → Allow →
streams; reconnect → no dialog (remembered). Commit:
`feat(gui): approve-on-host dialog + remembered ids`.

---

### Task 7: Android — split into ConnectActivity (launcher) + StreamActivity

**Files:**
- Rename: `android/.../ui/MainActivity.kt` → `android/.../ui/StreamActivity.kt` (class `StreamActivity`)
- Create: `android/.../ui/ConnectActivity.kt`, `res/layout/activity_connect.xml`, `res/values/themes.xml`
- Modify: `AndroidManifest.xml`, `net/TransportClient.kt`

**Interfaces:**
- Consumes: `Protocol.encodeHello(...)` v3, `DeviceIdentity`.
- Produces: `StreamActivity` reads `intent.getStringExtra("host")` / `getIntExtra("port", 27000)`
  and connects there (replacing the hardcoded `127.0.0.1`). `ConnectActivity.connectTo(host, port)`
  launches `StreamActivity` with those extras.

- [ ] **Step 1: Rename + parameterize StreamActivity.** Rename the file/class to
`StreamActivity`. Replace `HOST`/`PORT` constants usage in `startStreaming()` with values
from the intent: `private val host by lazy { intent.getStringExtra("host") ?: "127.0.0.1" }`
and `private val port by lazy { intent.getIntExtra("port", 27000) }`; pass `host, port` to
`c.run(...)`. Keep `screenOrientation=fullSensor` + fullscreen theme for this activity.

- [ ] **Step 2: TransportClient sends name+id.** Change `run(...)` to also take
`name: String, id: String` and send `Protocol.encodeHello(VERSION, width, height, density, name, id)`.
`StreamActivity` passes `DeviceIdentity.displayName(this)` / `stableId(this)`.

- [ ] **Step 3: ConnectActivity + layout (Material dark).** `activity_connect.xml`: a
`RecyclerView`/`ListView` `@+id/pc_list` for discovered PCs, an `EditText @+id/manual_addr`
(hint `192.168.1.50:27000`), a `Button @+id/connect_btn`, a `TextView @+id/status`, on a
`#1b1f24` background with teal accents (mirror the host theme colors). `ConnectActivity`:
populates the list from Discovery (Task 8), wires manual Connect to parse `host[:port]` and
call `connectTo`, and a "Reconnect to last" affordance reading the saved endpoint from
SharedPreferences (saved by `StreamActivity` on a successful connect).

```kotlin
fun connectTo(host: String, port: Int) {
    getSharedPreferences("droppix", MODE_PRIVATE).edit()
        .putString("last_host", host).putInt("last_port", port).apply()
    startActivity(Intent(this, StreamActivity::class.java)
        .putExtra("host", host).putExtra("port", port))
}
```

- [ ] **Step 4: themes.xml + manifest.** Add a `Theme.Droppix` (Material3/AppCompat dark);
add `com.google.android.material:material` to `app/build.gradle.kts` deps. Manifest: make
`ConnectActivity` the LAUNCHER (MAIN/LAUNCHER intent-filter); `StreamActivity` exported=false,
no launcher filter, keeps `fullSensor`/configChanges; add
`<uses-permission android:name="android.permission.ACCESS_WIFI_STATE"/>` and
`ACCESS_NETWORK_STATE`.

- [ ] **Step 5: Build** — `bash gradlew --no-daemon :app:assembleDebug` → BUILD SUCCESSFUL.
Manual: launch app → Connect screen shows; typing the PC's `ip:port` + Connect starts the
stream over WiFi; USB still works by typing `127.0.0.1:27000`.

- [ ] **Step 6: Commit** — `feat(android): ConnectActivity + StreamActivity split, WiFi connect`.

---

### Task 8: Android — NSD discovery of PCs

**Files:**
- Create: `android/.../net/Discovery.kt`
- Modify: `android/.../ui/ConnectActivity.kt` (use it)

**Interfaces:**
- Produces: `class Discovery(ctx) { fun start(onFound: (name:String, host:String, port:Int)->Unit, onLost:(name:String)->Unit); fun stop() }` — wraps `NsdManager` discover + resolve of `_droppix._tcp`.

- [ ] **Step 1: Implement `Discovery.kt`** using `NsdManager.discoverServices("_droppix._tcp",
PROTOCOL_DNS_SD, listener)`, resolving each found service (`resolveService`) to get
`serviceInfo.host.hostAddress` + `port`, invoking `onFound` on the main thread; `onLost` on
service-lost. Guard against the known NSD "resolve already in progress" by serializing
resolves (a simple queue).

- [ ] **Step 2: ConnectActivity uses it.** In `onResume`: `discovery.start({ name, host, port ->
runOnUiThread { add/replace in the list adapter } }, { name -> remove })`; `onPause`:
`discovery.stop()`. Tapping a list row calls `connectTo(host, port)`.

- [ ] **Step 3: Build + manual e2e** — `:app:assembleDebug`; with the host GUI streaming
(Task 5 advertising), the tablet's Connect screen lists the PC by name; tap → connects over
WiFi; approval dialog appears on the PC the first time (Task 6). USB path still works.

- [ ] **Step 4: Commit** — `feat(android): NSD discovery of droppix hosts`.

---

## Self-Review

- **Spec coverage:** HELLO name/id (T1,T2); approve-on-host gate + unified stdin (T3,T4);
  remembered ids + dialog (T6); host advertise (T5); Android Connect GUI + manual IP +
  reconnect-last (T7); NSD browse (T8); USB still works (T7 localhost target). Part-2 items
  (tablet advertise, wake, host browse/Devices panel) are intentionally a separate plan.
- **Placeholder scan:** none — every code step has concrete code; manual-verification steps
  (sockets/NSD/GUI) give exact commands + expected observations where unit tests don't apply.
- **Type consistency:** `encode_hello`/`encodeHello` v3 signatures match across T1/T2;
  `ApprovalGate`/`parse_approval` used by T3→T4; `writeLine`/`approvalRequested` defined in
  T6 and used in T6; `connectTo(host,port)` defined T7 used T7/T8.

## Execution

Implemented in order T1→T8. Part 2 (PC scans for tablets + wake) gets its own plan after
Part 1 is verified on-device.
