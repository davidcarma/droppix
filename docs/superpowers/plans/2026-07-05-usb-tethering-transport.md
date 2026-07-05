# USB Tethering Transport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect over USB without Android USB debugging by using USB tethering + a UDP probe/reply discovery, reusing droppix's existing IP transport, and removing the adb USB path.

**Architecture:** A tethered tablet is an IP device on the USB-local subnet; droppix reuses TLS/WAKE/streaming unchanged. A new mDNS-independent UDP probe/reply discovers the tablet over the tether link; a host `TetherScanner` (replacing the adb `UsbClientScanner`) surfaces it into the existing device-list + auto-connect path. Sessions dedup by device id so a tablet on Wi-Fi + USB yields one monitor.

**Tech Stack:** C++17 host (Qt6 Widgets, GoogleTest/ctest); Android client (Kotlin, JUnit); UDP datagrams.

## Global Constraints

- Fixed discovery UDP port **27010** (distinct from stream ports 27000–27003).
- **Probe** datagram: ASCII `"DPXQ"`, 4 bytes, no payload.
- **Reply** datagram: `"DPXR"` (4) · `u16 wakePort` big-endian · `u8 idLen` · `id[idLen]` · `u8 nameLen` · `name[nameLen]`. `id` = `DeviceIdentity.stableId`, `name` = `DeviceIdentity.displayName`. Decoders reject bad magic and declared lengths exceeding the datagram bounds.
- Codecs are **byte-identical C++↔Kotlin**, locked by the shared test vector: `wakePort=40000, id="abc", name="Nexus 10"` → bytes `44 50 58 52 9C 40 03 61 62 63 08 4E 65 78 75 73 20 31 30` (19 bytes).
- The adb USB path is **removed entirely** (no `adb devices`, `adb reverse`, `am start`, `--adb-reverse`, or `usb_autoconnect`).
- Tethered tablets connect via the **existing net/WAKE path**; the old `"usb"` transport tag collapses into `"net"` + a "— USB" label. No new connect logic.
- **Dedup by device id** across transports; the already-merged auto-connect `id` (mDNS TXT) is the same identity.
- Build C++ in the `droppix-dev` distrobox at `/home/Spinjitsudoomyt/droppix-build`; build/test Kotlin in `droppix-android`. Commits: `git -c user.name="Claude" -c user.email="noreply@anthropic.com"`.

---

## File Structure

- **Create** `host/src/tether_discovery.{h,cpp}` — pure probe/reply codec (host).
- **Create** `host/tests/test_tether_discovery.cpp` — codec tests.
- **Create** `host/gui/tether_scanner.{h,cpp}` — UDP broadcast probe + reply collection → discovered clients.
- **Create** `android/app/src/main/java/com/droppix/app/net/TetherProbe.kt` — pure codec (tablet).
- **Create** `android/app/src/test/java/com/droppix/app/net/TetherProbeTest.kt` — codec tests (shared vector).
- **Modify** `android/app/src/main/java/com/droppix/app/net/WakeService.kt` — answer probes with a reply.
- **Modify** `host/gui/session_manager.{h,cpp}` (+test) — `Session.id`, `SessionManager::ids()`.
- **Modify** `host/gui/auto_connect.{h,cpp}` (+test) — dedup by id.
- **Modify** `host/gui/main_window.{h,cpp}` — swap scanner, tethered→net path, dedup, remove adb.
- **Modify** `host/gui/args_builder.{h,cpp}`, `host/src/stream_main.cpp` — remove `--adb-reverse`.
- **Modify** `android/app/src/main/java/com/droppix/app/ui/ConnectActivity.kt` (+ layout) — remove USB one-tap + intent.
- **Delete** `host/gui/usb_client_scanner.{h,cpp}`, `host/src/usb_scan.{h,cpp}`, `host/tests/test_usb_scan.cpp`, `host/gui/adb_manager.{h,cpp}`.
- **Modify** `host/CMakeLists.txt` — add/remove sources.

---

### Task 1: Host probe/reply codec (pure)

**Files:**
- Create: `host/src/tether_discovery.h`, `host/src/tether_discovery.cpp`
- Create: `host/tests/test_tether_discovery.cpp`
- Modify: `host/CMakeLists.txt` (add `src/tether_discovery.cpp` to `droppix_core` sources; add `tests/test_tether_discovery.cpp` to `droppix_tests`)

**Interfaces:**
- Produces:
  - `constexpr uint16_t droppix::kTetherDiscoveryPort = 27010;`
  - `std::vector<unsigned char> droppix::encode_probe();`
  - `bool droppix::is_probe(const std::vector<unsigned char>&);`
  - `struct droppix::TetherReply { uint16_t wake_port = 0; std::string id; std::string name; };`
  - `std::vector<unsigned char> droppix::encode_reply(const TetherReply&);`
  - `bool droppix::decode_reply(const std::vector<unsigned char>&, TetherReply& out);`
- Consumed by Task 4 (`TetherScanner`).

- [ ] **Step 1: Write the failing tests**

Create `host/tests/test_tether_discovery.cpp`:

```cpp
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
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake . >/dev/null && cmake --build . --target droppix_tests -j$(nproc)'`
Expected: FAIL — `tether_discovery.h` not found.

- [ ] **Step 3: Create the header**

Create `host/src/tether_discovery.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace droppix {

// Fixed UDP port the tablet listens on for tether-discovery probes.
constexpr uint16_t kTetherDiscoveryPort = 27010;

// Probe (host -> tablet): ASCII "DPXQ", 4 bytes, no payload.
inline std::vector<unsigned char> encode_probe() { return {'D','P','X','Q'}; }
inline bool is_probe(const std::vector<unsigned char>& b) {
  return b.size() == 4 && b[0]=='D' && b[1]=='P' && b[2]=='X' && b[3]=='Q';
}

// Reply (tablet -> host): "DPXR" u16 wakePort(BE) u8 idLen id[] u8 nameLen name[].
struct TetherReply { uint16_t wake_port = 0; std::string id; std::string name; };
std::vector<unsigned char> encode_reply(const TetherReply& r);
bool decode_reply(const std::vector<unsigned char>& b, TetherReply& out);

}  // namespace droppix
```

- [ ] **Step 4: Create the implementation**

Create `host/src/tether_discovery.cpp`:

```cpp
#include "tether_discovery.h"

namespace droppix {

std::vector<unsigned char> encode_reply(const TetherReply& r) {
  std::vector<unsigned char> b = {'D','P','X','R',
    (unsigned char)(r.wake_port >> 8), (unsigned char)(r.wake_port & 0xFF)};
  b.push_back((unsigned char)(r.id.size() & 0xFF));
  b.insert(b.end(), r.id.begin(), r.id.end());
  b.push_back((unsigned char)(r.name.size() & 0xFF));
  b.insert(b.end(), r.name.begin(), r.name.end());
  return b;
}

bool decode_reply(const std::vector<unsigned char>& b, TetherReply& out) {
  if (b.size() < 7 || b[0]!='D'||b[1]!='P'||b[2]!='X'||b[3]!='R') return false;
  out.wake_port = (uint16_t(b[4]) << 8) | b[5];
  size_t i = 6;
  size_t idLen = b[i++];
  if (i + idLen > b.size()) return false;
  out.id.assign(b.begin() + i, b.begin() + i + idLen); i += idLen;
  if (i >= b.size()) return false;
  size_t nameLen = b[i++];
  if (i + nameLen > b.size()) return false;
  out.name.assign(b.begin() + i, b.begin() + i + nameLen);
  return true;
}

}  // namespace droppix
```

- [ ] **Step 5: Wire CMake**

In `host/CMakeLists.txt`: add `src/tether_discovery.cpp` to the `droppix_core` source list (near `src/usb_scan.cpp`), and add `tests/test_tether_discovery.cpp` to the `droppix_tests` `add_executable` list (near `tests/test_wake.cpp`).

- [ ] **Step 6: Run the tests to verify they pass**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake . >/dev/null && cmake --build . --target droppix_tests -j$(nproc) && ctest --output-on-failure -R TetherDiscovery'`
Expected: PASS — 4 `TetherDiscovery.*` tests.

- [ ] **Step 7: Commit**

```bash
git add host/src/tether_discovery.h host/src/tether_discovery.cpp host/tests/test_tether_discovery.cpp host/CMakeLists.txt
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(usb-tether): host probe/reply discovery codec"
```

---

### Task 2: Tablet probe/reply codec (pure, shared vector)

**Files:**
- Create: `android/app/src/main/java/com/droppix/app/net/TetherProbe.kt`
- Create: `android/app/src/test/java/com/droppix/app/net/TetherProbeTest.kt`

**Interfaces:**
- Produces (in `object TetherProbe`):
  - `const val PORT = 27010`
  - `fun encodeProbe(): ByteArray`
  - `fun isProbe(b: ByteArray, len: Int): Boolean`
  - `data class Reply(val wakePort: Int, val id: String, val name: String)`
  - `fun encodeReply(wakePort: Int, id: String, name: String): ByteArray`
  - `fun decodeReply(b: ByteArray, len: Int): Reply?`
- Consumed by Task 3 (`WakeService`).

- [ ] **Step 1: Write the failing test**

Create `android/app/src/test/java/com/droppix/app/net/TetherProbeTest.kt`:

```kotlin
package com.droppix.app.net

import org.junit.Assert.*
import org.junit.Test

class TetherProbeTest {
    @Test fun probeIsExactMagic() {
        val p = TetherProbe.encodeProbe()
        assertArrayEquals(byteArrayOf('D'.code.toByte(),'P'.code.toByte(),'X'.code.toByte(),'Q'.code.toByte()), p)
        assertTrue(TetherProbe.isProbe(p, p.size))
        assertFalse(TetherProbe.isProbe(byteArrayOf('D'.code.toByte(),'P'.code.toByte(),'X'.code.toByte()), 3))
    }

    @Test fun replyMatchesSharedVector() {
        val b = TetherProbe.encodeReply(40000, "abc", "Nexus 10")
        val want = intArrayOf(0x44,0x50,0x58,0x52,0x9C,0x40,0x03,0x61,0x62,0x63,
                              0x08,0x4E,0x65,0x78,0x75,0x73,0x20,0x31,0x30)
            .map { it.toByte() }.toByteArray()
        assertArrayEquals(want, b)
    }

    @Test fun replyRoundTrips() {
        val b = TetherProbe.encodeReply(51234, "dev-xyz", "Pixel")
        val r = TetherProbe.decodeReply(b, b.size)!!
        assertEquals(51234, r.wakePort); assertEquals("dev-xyz", r.id); assertEquals("Pixel", r.name)
    }

    @Test fun decodeRejectsBadMagicAndTruncation() {
        assertNull(TetherProbe.decodeReply(byteArrayOf('X'.code.toByte(),'X'.code.toByte(),'X'.code.toByte(),'X'.code.toByte()), 4))
        assertNull(TetherProbe.decodeReply(byteArrayOf('D'.code.toByte(),'P'.code.toByte(),'X'.code.toByte(),'R'.code.toByte(),0x9C.toByte()), 5))
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle bash gradlew --no-daemon testDebugUnitTest --tests "com.droppix.app.net.TetherProbeTest"'`
Expected: FAIL — unresolved reference `TetherProbe`.

- [ ] **Step 3: Create the codec**

Create `android/app/src/main/java/com/droppix/app/net/TetherProbe.kt`:

```kotlin
package com.droppix.app.net

/**
 * Codec for tether-discovery datagrams (mDNS-independent USB-link discovery).
 * Probe: ASCII "DPXQ" (4 bytes). Reply: "DPXR" u16 wakePort(BE) u8 idLen id u8 nameLen name.
 * Byte-identical to the host codec (see host/src/tether_discovery.*).
 */
object TetherProbe {
    const val PORT = 27010

    fun encodeProbe(): ByteArray = byteArrayOf(
        'D'.code.toByte(), 'P'.code.toByte(), 'X'.code.toByte(), 'Q'.code.toByte())

    fun isProbe(b: ByteArray, len: Int): Boolean =
        len == 4 && b[0]=='D'.code.toByte() && b[1]=='P'.code.toByte() &&
        b[2]=='X'.code.toByte() && b[3]=='Q'.code.toByte()

    data class Reply(val wakePort: Int, val id: String, val name: String)

    fun encodeReply(wakePort: Int, id: String, name: String): ByteArray {
        val idB = id.toByteArray(Charsets.UTF_8)
        val nameB = name.toByteArray(Charsets.UTF_8)
        val out = ArrayList<Byte>()
        out.add('D'.code.toByte()); out.add('P'.code.toByte())
        out.add('X'.code.toByte()); out.add('R'.code.toByte())
        out.add(((wakePort ushr 8) and 0xFF).toByte()); out.add((wakePort and 0xFF).toByte())
        out.add((idB.size and 0xFF).toByte()); out.addAll(idB.toList())
        out.add((nameB.size and 0xFF).toByte()); out.addAll(nameB.toList())
        return out.toByteArray()
    }

    fun decodeReply(b: ByteArray, len: Int): Reply? {
        if (len < 7 || b[0]!='D'.code.toByte() || b[1]!='P'.code.toByte() ||
            b[2]!='X'.code.toByte() || b[3]!='R'.code.toByte()) return null
        val port = ((b[4].toInt() and 0xFF) shl 8) or (b[5].toInt() and 0xFF)
        var i = 6
        val idLen = b[i].toInt() and 0xFF; i++
        if (i + idLen > len) return null
        val id = String(b, i, idLen, Charsets.UTF_8); i += idLen
        if (i >= len) return null
        val nameLen = b[i].toInt() and 0xFF; i++
        if (i + nameLen > len) return null
        val name = String(b, i, nameLen, Charsets.UTF_8)
        return Reply(port, id, name)
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle bash gradlew --no-daemon testDebugUnitTest --tests "com.droppix.app.net.TetherProbeTest"'`
Expected: PASS — 4 tests.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/net/TetherProbe.kt android/app/src/test/java/com/droppix/app/net/TetherProbeTest.kt
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(usb-tether): tablet probe/reply codec (shared vector)"
```

---

### Task 3: Tablet discovery responder (in WakeService)

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/net/WakeService.kt`

**Interfaces:**
- Consumes: `TetherProbe` (Task 2), `DeviceIdentity`.
- Produces: while the connect screen is active, the tablet answers a `DPXQ` probe on UDP `27010` with a `DPXR` reply carrying its wake port + id + name.

**Context:** `WakeService.start()` already binds a `DatagramSocket(0)` (the wake port = `sock.localPort`), registers mDNS, and runs `receiveLoop`. Add a **second** socket bound to `TetherProbe.PORT` with its own receive loop that answers probes with a reply built from `localPort` + `DeviceIdentity`.

- [ ] **Step 1: Add the discovery-responder fields and startup**

In `WakeService.kt`, add fields near the existing `socket`/`receiveThread`:

```kotlin
    private var discoverySocket: DatagramSocket? = null
    private var discoveryThread: Thread? = null
```

In `start(...)`, after `val localPort = sock.localPort`, add:

```kotlin
        val discSock = try { DatagramSocket(null).apply {
            reuseAddress = true; bind(java.net.InetSocketAddress(TetherProbe.PORT))
        } } catch (e: Exception) {
            Log.w(TAG, "tether-discovery bind failed: ${e.message}"); null
        }
        discoverySocket = discSock
        if (discSock != null) {
            val name = DeviceIdentity.displayName(ctx)
            val id = DeviceIdentity.stableId(ctx)
            val t = Thread({ discoveryLoop(discSock, localPort, id, name) }, "TetherDiscovery-recv")
            discoveryThread = t; t.start()
        }
```

- [ ] **Step 2: Add the responder loop**

Add this method to `WakeService`:

```kotlin
    private fun discoveryLoop(sock: DatagramSocket, wakePort: Int, id: String, name: String) {
        val buf = ByteArray(64)
        val pkt = DatagramPacket(buf, buf.size)
        while (running.get()) {
            try { sock.receive(pkt) } catch (e: SocketException) { break }
              catch (e: Exception) { if (running.get()) Log.w(TAG, "discovery recv: ${e.message}"); break }
            if (TetherProbe.isProbe(pkt.data, pkt.length)) {
                val reply = TetherProbe.encodeReply(wakePort, id, name)
                try { sock.send(DatagramPacket(reply, reply.size, pkt.address, pkt.port)) }
                catch (e: Exception) { Log.w(TAG, "discovery reply: ${e.message}") }
            }
        }
    }
```

- [ ] **Step 3: Tear it down in stop()**

In `stop()`, after the existing `socket?.close(); socket = null`, add:

```kotlin
        discoverySocket?.close(); discoverySocket = null
        discoveryThread?.let { if (it != Thread.currentThread()) try { it.join(1000) } catch (_: InterruptedException) {} }
        discoveryThread = null
```

- [ ] **Step 4: Build the app + run existing net tests**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle bash gradlew --no-daemon assembleDebug testDebugUnitTest'`
Expected: BUILD SUCCESSFUL; all unit tests pass.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/net/WakeService.kt
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(usb-tether): tablet answers discovery probes with its wake port + id"
```

---

### Task 4: Host `TetherScanner`

**Files:**
- Create: `host/gui/tether_scanner.h`, `host/gui/tether_scanner.cpp`
- Modify: `host/CMakeLists.txt` (add `gui/tether_scanner.cpp` to `droppix_gui`)

**Interfaces:**
- Consumes: `encode_probe()`, `decode_reply()`, `kTetherDiscoveryPort` (Task 1).
- Produces:
  - `struct droppix::TetherClient { QString id; QString name; QString address; quint16 wakePort = 0; };`
  - `class TetherScanner : public QObject` with `bool available() const; void start(); void stop();` and `signals: void clientsChanged(QList<droppix::TetherClient>);`
- Consumed by Task 6 (`MainWindow`).

- [ ] **Step 1: Create the header**

Create `host/gui/tether_scanner.h`:

```cpp
#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QList>
#include <QString>
#include <QMetaType>

namespace droppix {

// A droppix tablet discovered over a (USB-tether) IP link via UDP probe/reply.
struct TetherClient {
  QString id;         // DeviceIdentity.stableId (cross-transport identity)
  QString name;       // display name
  QString address;    // tablet IP (reply source)
  quint16 wakePort = 0;
};

// Every ~2s broadcasts a "DPXQ" probe on each non-loopback interface and collects
// "DPXR" replies for a short window, emitting the discovered tablets. mDNS-independent,
// so it works across the USB-tether link where Android mDNS is unreliable. Replaces the
// adb-based UsbClientScanner.
class TetherScanner : public QObject {
  Q_OBJECT
 public:
  explicit TetherScanner(QObject* p = nullptr);
  bool available() const { return true; }   // no external tool needed
  void start();
  void stop();
 signals:
  void clientsChanged(QList<droppix::TetherClient> clients);
 private:
  void tick();                  // send probes, arm the collection window
  void onDatagram();            // parse an incoming reply
  QUdpSocket sock_;
  QTimer timer_;                // scan cadence
  QTimer window_;               // per-scan reply-collection window
  QList<TetherClient> acc_;     // replies this scan
};

}  // namespace droppix

Q_DECLARE_METATYPE(droppix::TetherClient)
```

- [ ] **Step 2: Create the implementation**

Create `host/gui/tether_scanner.cpp`:

```cpp
#include "tether_scanner.h"
#include "tether_discovery.h"
#include <QNetworkInterface>

namespace droppix {

TetherScanner::TetherScanner(QObject* p) : QObject(p) {
  sock_.bind(QHostAddress::AnyIPv4, 0, QAbstractSocket::ShareAddress);
  connect(&sock_, &QUdpSocket::readyRead, this, &TetherScanner::onDatagram);
  timer_.setInterval(2000);
  connect(&timer_, &QTimer::timeout, this, &TetherScanner::tick);
  window_.setSingleShot(true);
  window_.setInterval(600);   // collect replies for 600ms, then emit
  connect(&window_, &QTimer::timeout, this, [this]{ emit clientsChanged(acc_); });
}

void TetherScanner::start() { tick(); timer_.start(); }
void TetherScanner::stop()  { timer_.stop(); window_.stop(); }

void TetherScanner::tick() {
  acc_.clear();
  const auto probe = encode_probe();
  const QByteArray dg(reinterpret_cast<const char*>(probe.data()), (int)probe.size());
  for (const auto& iface : QNetworkInterface::allInterfaces()) {
    if (!(iface.flags() & QNetworkInterface::IsUp) ||
        (iface.flags() & QNetworkInterface::IsLoopBack)) continue;
    for (const auto& entry : iface.addressEntries()) {
      const QHostAddress bc = entry.broadcast();
      if (!bc.isNull()) sock_.writeDatagram(dg, bc, kTetherDiscoveryPort);
    }
  }
  window_.start();   // (re)arm; emit the collected set when it fires
}

void TetherScanner::onDatagram() {
  while (sock_.hasPendingDatagrams()) {
    QByteArray buf; buf.resize((int)sock_.pendingDatagramSize());
    QHostAddress from;
    sock_.readDatagram(buf.data(), buf.size(), &from);
    std::vector<unsigned char> b(buf.begin(), buf.end());
    TetherReply r;
    if (!decode_reply(b, r)) continue;
    TetherClient c;
    c.id = QString::fromStdString(r.id);
    c.name = QString::fromStdString(r.name);
    c.address = from.toString();
    // QHostAddress may render IPv4 as "::ffff:1.2.3.4"; keep the tail.
    if (c.address.startsWith("::ffff:")) c.address = c.address.mid(7);
    c.wakePort = r.wake_port;
    bool dup = false;
    for (const auto& e : acc_) if (e.id == c.id) { dup = true; break; }
    if (!dup) acc_.push_back(c);
  }
}

}  // namespace droppix
```

- [ ] **Step 3: Wire CMake + build the GUI**

In `host/CMakeLists.txt`, add `gui/tether_scanner.cpp` to the `droppix_gui` target sources (near `gui/mdns_browser.cpp`).

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake . >/dev/null && cmake --build . --target droppix_gui -j$(nproc)'`
Expected: builds clean (moc runs on the new QObject).

- [ ] **Step 4: Commit**

```bash
git add host/gui/tether_scanner.h host/gui/tether_scanner.cpp host/CMakeLists.txt
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(usb-tether): host TetherScanner (UDP broadcast probe + reply collection)"
```

---

### Task 5: Cross-transport dedup by device id

**Files:**
- Modify: `host/gui/session_manager.h`, `host/gui/session_manager.cpp`
- Modify: `host/gui/tests/test_session_manager.cpp`
- Modify: `host/gui/auto_connect.h`, `host/gui/auto_connect.cpp`
- Modify: `host/gui/tests/test_auto_connect.cpp`

**Interfaces:**
- Produces:
  - `Session.id` (QString) — the tablet's device id.
  - `QSet<QString> SessionManager::ids() const;`
  - `AutoConnectCandidate` gains `QString id;`
  - `devicesToConnect(bool enabled, const QList<AutoConnectCandidate>&, const QSet<QString>& activeKeys, const QSet<QString>& activeIds)` — excludes a candidate whose key OR non-empty id is already active.
- Consumed by Task 6.

- [ ] **Step 1: Write the failing tests**

Append to `host/gui/tests/test_session_manager.cpp`:

```cpp
TEST(SessionManager, IdsListsNonEmptyIds) {
  SessionManager m;
  Session a; a.key = "net:1"; a.id = "dev-a"; m.add(a);
  Session b; b.key = "net:2"; b.id = "";      m.add(b);   // no id -> not listed
  EXPECT_EQ(m.ids(), (QSet<QString>{QString("dev-a")}));
}
```

Append to `host/gui/tests/test_auto_connect.cpp`:

```cpp
static AutoConnectCandidate candId(const QString& k, const QString& id, bool e) {
  AutoConnectCandidate c; c.key = k; c.id = id; c.eligible = e; return c;
}

TEST(AutoConnect, ExcludesCandidateWhoseIdIsActive) {
  QList<AutoConnectCandidate> cs = {candId("net:usb", "dev-x", true)};
  auto r = devicesToConnect(true, cs, /*activeKeys*/{}, /*activeIds*/{QString("dev-x")});
  EXPECT_TRUE(r.isEmpty());   // same tablet already connected on the other transport
}

TEST(AutoConnect, EmptyIdNotDedupedById) {
  QList<AutoConnectCandidate> cs = {candId("net:a", "", true)};
  auto r = devicesToConnect(true, cs, {}, {QString("")});
  ASSERT_EQ(r.size(), 1); EXPECT_EQ(r[0], "net:a");
}
```

Update the existing `test_auto_connect.cpp` calls: every existing `devicesToConnect(enabled, cands, active)` call gains a 4th arg `{}` (empty `activeIds`). (The existing `cand()` helper leaves `id` empty, which the new tests confirm is not deduped.)

- [ ] **Step 2: Run the tests to verify they fail**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . --target droppix_gui_tests -j$(nproc)'`
Expected: FAIL — `Session` has no `id`; `devicesToConnect` takes 3 args; `AutoConnectCandidate` has no `id`.

- [ ] **Step 3: Add `Session.id` + `SessionManager::ids()`**

In `host/gui/session_manager.h`, add to `struct Session` after `transport`:

```cpp
  QString id;          // tablet device id (cross-transport identity; may be empty)
```

Declare in the class (near `keys()`):

```cpp
  QSet<QString> ids() const;   // non-empty device ids of active sessions
```

In `host/gui/session_manager.cpp`, add:

```cpp
QSet<QString> SessionManager::ids() const {
  QSet<QString> s;
  for (const auto& x : sessions_) if (!x.id.isEmpty()) s.insert(x.id);
  return s;
}
```

- [ ] **Step 4: Add id to the policy**

In `host/gui/auto_connect.h`, add `QString id;` to `AutoConnectCandidate` and change the signature:

```cpp
struct AutoConnectCandidate { QString key; QString id; bool eligible = false; };

QList<QString> devicesToConnect(bool enabled,
                                const QList<AutoConnectCandidate>& candidates,
                                const QSet<QString>& activeKeys,
                                const QSet<QString>& activeIds);
```

In `host/gui/auto_connect.cpp`:

```cpp
QList<QString> devicesToConnect(bool enabled,
                                const QList<AutoConnectCandidate>& candidates,
                                const QSet<QString>& activeKeys,
                                const QSet<QString>& activeIds) {
  QList<QString> out;
  if (!enabled) return out;
  for (const auto& c : candidates) {
    if (!c.eligible) continue;
    if (activeKeys.contains(c.key)) continue;
    if (!c.id.isEmpty() && activeIds.contains(c.id)) continue;
    out.push_back(c.key);
  }
  return out;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . --target droppix_gui_tests -j$(nproc) && ctest --output-on-failure -R "AutoConnect|SessionManager"'`
Expected: PASS.

> **Note (expected):** this task's gate is the **`droppix_gui_tests`** binary (it links `auto_connect.cpp` + `session_manager.cpp` + the updated tests, not `main_window.cpp`). The **`droppix_gui`** binary will NOT compile until Task 6 updates its call sites to the new `devicesToConnect(...)` signature and `startSession`/`connectDevice` — that is expected and intentional; do not try to build `droppix_gui` in this task.

- [ ] **Step 6: Commit**

```bash
git add host/gui/session_manager.h host/gui/session_manager.cpp host/gui/tests/test_session_manager.cpp host/gui/auto_connect.h host/gui/auto_connect.cpp host/gui/tests/test_auto_connect.cpp
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(usb-tether): dedup sessions by device id across transports"
```

---

### Task 6: Rewire the GUI to tether discovery; remove the adb host path

**Files:**
- Modify: `host/gui/main_window.h`, `host/gui/main_window.cpp`
- Modify: `host/gui/args_builder.h`, `host/gui/args_builder.cpp`
- Modify: `host/src/stream_main.cpp`
- Delete: `host/gui/usb_client_scanner.h`, `host/gui/usb_client_scanner.cpp`, `host/src/usb_scan.h`, `host/src/usb_scan.cpp`, `host/tests/test_usb_scan.cpp`, `host/gui/adb_manager.h`, `host/gui/adb_manager.cpp`
- Modify: `host/CMakeLists.txt` (drop the deleted sources; keep `gui/tether_scanner.cpp`)

**Interfaces:**
- Consumes: `TetherScanner`/`TetherClient` (Task 4), `Session.id`/`SessionManager::ids()`/`devicesToConnect(...,activeIds)` (Task 5).
- Produces: tethered tablets appear in the device list labeled "*Name* — USB", carry `id` (UserRole+3), address (UserRole+1), and wake port (UserRole+2), and connect via the net/WAKE path; `startSession` records `Session.id`.

**Context (current state to change):**
- `main_window.cpp:254-268` wire `usbScanner_` (`UsbClientScanner`) → `onUsbClientsChanged` → `usbClients_` → `rebuildClientList`.
- `rebuildClientList` (`main_window.cpp:~380-397`) builds a `usb` item per `UsbClient` and a `net` item per `MdnsDevice` (id at `UserRole+3`).
- `connectDevice` (`main_window.cpp:~436`) has a `transport == "usb"` branch calling `adb_.usbConnect`.
- `evaluateAutoConnect` builds `usb` + `net` candidates.
- `startSession` calls `adb_.setupReverse(port)` when `cmd.needs_adb_reverse`.

- [ ] **Step 1: Swap the scanner member**

In `host/gui/main_window.h`: replace `#include "adb_manager.h"` with `#include "tether_scanner.h"`; replace `AdbManager adb_;` and `UsbClientScanner usbScanner_;` with `TetherScanner tetherScanner_;`; replace `QList<UsbClient> usbClients_;` with `QList<TetherClient> tetherClients_;`; rename the slot `void onUsbClientsChanged(const QList<UsbClient>&);` to `void onTetherClientsChanged(const QList<TetherClient>&);`. Remove the `UsbClientScanner`/`UsbClient`/`AdbManager` includes/usages.

- [ ] **Step 2: Rewire discovery in the constructor**

In `host/gui/main_window.cpp`, replace the `usbScanner_` wiring block (around lines 254-268) with:

```cpp
  connect(&tetherScanner_, &TetherScanner::clientsChanged, this, &MainWindow::onTetherClientsChanged);
  // ... keep the browser_ wiring as-is ...
  if (tetherScanner_.available()) tetherScanner_.start();
  if (!browser_.available()) { /* tether always available; keep devicesBox_ visible */ }
```

Replace `onUsbClientsChanged` with:

```cpp
void MainWindow::onTetherClientsChanged(const QList<TetherClient>& clients) {
  tetherClients_ = clients;
  rebuildClientList();
  autoConnectTimer_.start();
}
```

- [ ] **Step 2b: Rebuild the list with tethered tablets as net+USB**

In `rebuildClientList()`, replace the `for (const auto& c : usbClients_)` loop with:

```cpp
  for (const auto& t : tetherClients_) {
    auto* item = new QListWidgetItem(QString("%1 — USB").arg(t.name));
    item->setData(Qt::UserRole, "net");                 // connects via WAKE like Wi-Fi
    item->setData(Qt::UserRole + 1, t.address);
    item->setData(Qt::UserRole + 2, (uint)t.wakePort);
    item->setData(Qt::UserRole + 3, t.id);
    devicesList_->addItem(item);
    if (item->text() == prevSelected) devicesList_->setCurrentItem(item);
  }
```

- [ ] **Step 3: Remove the adb branch in `connectDevice`; record the id**

`connectDevice`'s signature stays, but drop the `usb` branch. Since every device is now `"net"`, `connectDevice` always does the WAKE path. Change its body so the `transport == "usb"` branch is removed (the adb lambda deleted). Then thread the id into the session: add a `const QString& id` parameter to `connectDevice` and pass it to `startSession`. New `connectDevice` signature:

```cpp
bool connectDevice(const QString& key, const QString& label, const QString& transport,
                   const QString& ident, quint16 wakePort, const QString& id, bool quietIfBusy);
```

Body (WAKE-only):

```cpp
bool MainWindow::connectDevice(const QString& key, const QString& label, const QString& transport,
                               const QString& ident, quint16 wakePort, const QString& id,
                               bool quietIfBusy) {
  if (sessions_.has(key) || (!id.isEmpty() && sessions_.ids().contains(id))) {
    if (!quietIfBusy) QMessageBox::information(this, "Droppix", "That device already has an active monitor.");
    return false;
  }
  const int port = sessions_.allocatePort(collectSettings().port);
  if (port < 0) { if (!quietIfBusy) QMessageBox::information(this, "Droppix", "Monitor limit reached (4)."); return false; }
  const QString addr = ident;
  auto direct = [this, addr, wakePort, port]{
    auto bytes = encode_wake((uint16_t)port);
    QByteArray dg(reinterpret_cast<const char*>(bytes.data()), (int)bytes.size());
    pendingWakes_[addr] = QDateTime::currentMSecsSinceEpoch();
    QUdpSocket sock; sock.writeDatagram(dg, QHostAddress(addr), wakePort);
  };
  startSession(key, label, transport, port, id, direct);
  return true;
}
```

- [ ] **Step 4: Thread the id through `startSession`**

Add `const QString& id` to `startSession`'s signature (`main_window.h` + `.cpp`) and set `sess.id = id;` where the `Session` is populated:

```cpp
void MainWindow::startSession(const QString& key, const QString& label, const QString& transport,
                              int port, const QString& id, std::function<void()> directTablet) {
  // ... unchanged until the Session is built ...
  sess.transport = transport; sess.id = id; sess.touchName = QString::fromStdString(tname);
```

Update the `onStartStop()` call site (`startSession(QString("waiting:%1")..., port, ..., {})`) to pass `""` for id: `startSession(QString("waiting:%1").arg(port), "Waiting for a tablet…", QString(), port, QString(), {});`.

- [ ] **Step 5: Update `onConnectToSelectedDevice` + `evaluateAutoConnect`**

`onConnectToSelectedDevice` reads the id from `UserRole+3` and passes it:

```cpp
  const QString id = item->data(Qt::UserRole + 3).toString();
  const quint16 wakePort = (quint16)item->data(Qt::UserRole + 2).toUInt();
  connectDevice(key, label, transport, ident, wakePort, id, /*quietIfBusy=*/false);
```

`evaluateAutoConnect`: drop the USB candidate loop; build net candidates (mDNS + tether both live in the list as `net` items). Simplest: build candidates from `devicesList_` items directly so both discovery sources are covered uniformly:

```cpp
void MainWindow::evaluateAutoConnect() {
  if (!collectSettings().autoConnect) return;
  QList<AutoConnectCandidate> cands;
  for (int i = 0; i < devicesList_->count(); ++i) {
    auto* it = devicesList_->item(i);
    const QString addr = it->data(Qt::UserRole + 1).toString();
    const QString id = it->data(Qt::UserRole + 3).toString();
    cands.push_back({QString("net:") + addr, id, !id.isEmpty() && approved_.isApproved(id)});
  }
  const QList<QString> toConnect = devicesToConnect(true, cands, sessions_.keys(), sessions_.ids());
  for (const QString& key : toConnect) {
    const QString addr = key.mid(4);
    for (int i = 0; i < devicesList_->count(); ++i) {
      auto* it = devicesList_->item(i);
      if (it->data(Qt::UserRole + 1).toString() != addr) continue;
      connectDevice(key, it->text(), "net", addr,
                    (quint16)it->data(Qt::UserRole + 2).toUInt(),
                    it->data(Qt::UserRole + 3).toString(), /*quietIfBusy=*/true);
      break;
    }
  }
}
```

- [ ] **Step 6: Remove `setupReverse` + `--adb-reverse`**

- In `startSession`, delete the `if (cmd.needs_adb_reverse) adb_.setupReverse(port);` line.
- In `host/gui/args_builder.h`, remove `bool needs_adb_reverse = false;`; in `args_builder.cpp`, remove the `c.needs_adb_reverse = s.auto_adb_reverse;` line (and any `--adb-reverse` emission).
- In `host/src/stream_main.cpp`, remove the `adb_reverse` variable, the `--adb-reverse` arg parse, and the `if (adb_reverse) { adb reverse ... }` block.

- [ ] **Step 7: Delete the adb files + fix CMake**

```bash
git rm host/gui/usb_client_scanner.h host/gui/usb_client_scanner.cpp host/src/usb_scan.h host/src/usb_scan.cpp host/tests/test_usb_scan.cpp host/gui/adb_manager.h host/gui/adb_manager.cpp
```

In `host/CMakeLists.txt`, remove the lines: `src/usb_scan.cpp` (both occurrences — `droppix_core` and the `droppix_tests` extra sources), `gui/usb_client_scanner.cpp`, `gui/adb_manager.cpp`, and `tests/test_usb_scan.cpp`.

- [ ] **Step 8: Build everything + run the full host suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake . >/dev/null && cmake --build . -j$(nproc) && ctest --output-on-failure'`
Expected: builds clean; 100% tests pass (the `UsbScan` tests are gone; `TetherDiscovery`, `AutoConnect`, `SessionManager` pass).

- [ ] **Step 9: Commit**

```bash
git add -A host/gui host/src host/CMakeLists.txt
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(usb-tether): rewire GUI to tether discovery; remove the adb USB path"
```

---

### Task 7: Remove the adb USB path on the tablet

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/ui/ConnectActivity.kt`
- Modify: `android/app/src/main/res/layout/activity_connect.xml`

**Context:** `ConnectActivity.onCreate` wires a `usb_btn` (`findViewById<ImageButton>(R.id.usb_btn)`) that connects to `127.0.0.1:27000`, and handles a `usb_autoconnect` / `usb_port` launch intent. Both are the adb-reverse path and are removed. Discovery over USB now happens via tethering (the tablet just needs to be open on the connect screen).

- [ ] **Step 1: Remove the USB one-tap + intent handling**

In `ConnectActivity.kt`, delete the `findViewById<android.widget.ImageButton>(R.id.usb_btn).setOnClickListener { ... connectTo("127.0.0.1", 27000) }` block and the `if (intent.getBooleanExtra("usb_autoconnect", false)) { ... connectTo("127.0.0.1", ...) }` block.

- [ ] **Step 2: Remove the USB button from the layout**

In `android/app/src/main/res/layout/activity_connect.xml`, remove the `ImageButton` with `android:id="@+id/usb_btn"` (and any now-dangling label/constraint referencing it).

- [ ] **Step 3: Build the app + run unit tests**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle bash gradlew --no-daemon assembleDebug testDebugUnitTest'`
Expected: BUILD SUCCESSFUL; unit tests pass.

- [ ] **Step 4: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/ui/ConnectActivity.kt android/app/src/main/res/layout/activity_connect.xml
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(usb-tether): remove adb USB one-tap + autoconnect intent (tablet)"
```

---

### Task 8: End-to-end verification on hardware (manual)

**Files:** none (verification only).

- [ ] **Step 1:** Rebuild + install the APK on the tablet; rebuild the host GUI/AppImage.
- [ ] **Step 2:** On the tablet, turn **Developer Options OFF**. Plug in USB. Enable **Settings → Hotspot & tethering → USB tethering**. Open the droppix app (connect screen).
- [ ] **Step 3:** On the host, confirm the tablet appears in the device list as "*Name* — USB". With **auto-connect on**, confirm the monitor comes up with no clicks; else Connect it.
- [ ] **Step 4:** Verify streaming + touch work over USB with no adb and no Developer Options.
- [ ] **Step 5 (dedup):** Connect the same tablet over Wi-Fi *and* USB tethering simultaneously; confirm exactly **one** monitor (id dedup).
- [ ] **Step 6 (route hygiene):** Confirm the PC keeps its normal internet (if not, mark the USB NetworkManager connection `ipv4.never-default`).

---

## Notes for the implementer

- The shared codec vector (`wakePort=40000, id="abc", name="Nexus 10"` → the 19 bytes in Global Constraints) must produce identical bytes in Task 1 (C++) and Task 2 (Kotlin) — that is the C++↔Kotlin lock.
- Tethered tablets are `"net"` transport mechanically; the "— USB" label is display-only. That is why no new connect logic is needed (Task 6 reuses the WAKE path).
- After Task 6, the host no longer depends on `adb` at all.
