# Auto-Connect Known Monitors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When the host GUI is running, automatically connect trusted tablets (USB and paired Wi-Fi) instead of selecting each device and clicking Connect.

**Architecture:** A pure `AutoConnectPolicy` decides which discovered devices to connect; `MainWindow` builds candidates from the existing USB + mDNS discovery signals and drives them through the same connect path as a manual click. The tablet advertises its stable id in its mDNS TXT record so the host can match it against its approved store, and auto-accepts a WAKE from a host it has already paired.

**Tech Stack:** C++17 host GUI (Qt6 Widgets, GoogleTest via ctest); Android client (Kotlin, JUnit).

## Global Constraints

- One host toggle **"Auto-connect known monitors"**, `Settings.autoConnect`, **default `true`**. Off = today's fully-manual behavior.
- **USB eligibility:** any plugged-in tablet with the app installed (the USB scan already lists only those). No pairing concept.
- **Wi-Fi eligibility:** the discovered tablet's advertised TXT `id` is in the host approved store (`approved_.isApproved(id)`); empty id is never eligible.
- The advertised TXT `id` MUST equal `DeviceIdentity.stableId` (the same id the tablet sends in its HELLO and that the host stores as its approved-store key).
- Auto and manual connect share ONE code path (port allocation, streamer spawn, `usbConnect`/WAKE, session tracking, 4-monitor cap all apply unchanged).
- Toggling off does NOT tear down existing monitors.
- `sessions_.has(key)` is the re-fire guard — `startSession` registers the session synchronously, so a device mid-connect is already "active" and won't be re-selected.
- Build C++ in the `droppix-dev` distrobox at `/home/Spinjitsudoomyt/droppix-build`; build/test Kotlin in the `droppix-android` distrobox. Commits: `git -c user.name="Claude" -c user.email="noreply@anthropic.com"`.

---

## File Structure

Host:
- `host/src/mdns_browse.{h,cpp}` — add `id` to `MdnsDevice`, parse TXT `id` (pure).
- `host/tests/test_mdns_browse.cpp` — TXT id parse tests.
- `host/gui/auto_connect.{h,cpp}` (new) — pure `AutoConnectPolicy`.
- `host/gui/tests/test_auto_connect.cpp` (new) — policy tests.
- `host/gui/session_manager.{h,cpp}` — add `keys()`.
- `host/gui/tests/test_session_manager.cpp` — `keys()` test.
- `host/gui/settings.h` — add `autoConnect`.
- `host/gui/profile_store.cpp` — persist `auto_connect`.
- `host/gui/tests/test_profile_store.cpp` — round-trip assertion.
- `host/gui/settings_dialog.{h,cpp}` — checkbox + load/store.
- `host/gui/main_window.{h,cpp}` — surface id on net items; factor `connectDevice`; debounce timer; `evaluateAutoConnect`.
- `host/CMakeLists.txt` — wire `auto_connect.cpp` + test into `droppix_gui` and `droppix_gui_tests`.

Tablet:
- `android/app/src/main/java/com/droppix/app/net/AutoAccept.kt` (new) — `shouldAutoAccept`.
- `android/app/src/test/java/com/droppix/app/net/AutoAcceptTest.kt` (new) — test.
- `android/app/src/main/java/com/droppix/app/net/WakeService.kt` — add TXT `id`.
- `android/app/src/main/java/com/droppix/app/ui/ConnectActivity.kt` — auto-accept branch on WAKE.

---

### Task 1: Parse the tablet's TXT `id` (host, pure)

**Files:**
- Modify: `host/src/mdns_browse.h` (MdnsDevice struct)
- Modify: `host/src/mdns_browse.cpp` (`parse_avahi_browse`)
- Test: `host/tests/test_mdns_browse.cpp`

**Interfaces:**
- Consumes: existing `parse_avahi_browse(const std::string&) -> std::vector<MdnsDevice>`.
- Produces: `MdnsDevice` gains `std::string id;` (empty when no TXT `id`). Later tasks read `dev.id`.

- [ ] **Step 1: Write the failing tests**

Append to `host/tests/test_mdns_browse.cpp`:

```cpp
TEST(MdnsBrowse, ParsesTxtId) {
  auto v = parse_avahi_browse(
    "=;eth0;IPv4;Nexus 10;_droppix-client._tcp;local;h;192.168.1.42;48000;\"id=dev-abc-123\"\n");
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0].id, "dev-abc-123");
}

TEST(MdnsBrowse, EmptyTxtGivesEmptyId) {
  auto v = parse_avahi_browse(
    "=;eth0;IPv4;Nexus 10;_droppix-client._tcp;local;h;192.168.1.42;48000;\"\"\n");
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0].id, "");
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . --target droppix_tests -j$(nproc)'`
Expected: FAIL to compile — `MdnsDevice` has no member `id`.

- [ ] **Step 3: Add the `id` field**

In `host/src/mdns_browse.h`, add `id` to the struct:

```cpp
struct MdnsDevice {
  std::string name;
  std::string address;
  uint16_t port = 0;
  std::string id;   // TXT "id=<stableId>" (empty if absent); matches the host approved store
};
```

- [ ] **Step 4: Parse the TXT `id`**

In `host/src/mdns_browse.cpp`, add this helper above `parse_avahi_browse` (inside the anonymous namespace):

```cpp
// Extract the value of the `id=` TXT record from an avahi parseable txt field,
// which looks like: "id=abc-123" "other=x"  (each record double-quoted).
std::string parse_txt_id(const std::string& txt) {
  const std::string key = "id=";
  auto pos = txt.find(key);
  if (pos == std::string::npos) return "";
  pos += key.size();
  std::string val;
  for (; pos < txt.size() && txt[pos] != '"' && txt[pos] != ' '; ++pos) val.push_back(txt[pos]);
  return val;
}
```

Then, in `parse_avahi_browse`, after `dev.port = ...;` and before the dedup block, add:

```cpp
    if (fields.size() >= 10) dev.id = parse_txt_id(fields[9]);
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . --target droppix_tests -j$(nproc) && ctest --output-on-failure -R MdnsBrowse'`
Expected: PASS — all `MdnsBrowse.*` tests pass.

- [ ] **Step 6: Commit**

```bash
git add host/src/mdns_browse.h host/src/mdns_browse.cpp host/tests/test_mdns_browse.cpp
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(auto-connect): parse tablet TXT id from mDNS"
```

---

### Task 2: `AutoConnectPolicy` (host, pure)

**Files:**
- Create: `host/gui/auto_connect.h`
- Create: `host/gui/auto_connect.cpp`
- Create: `host/gui/tests/test_auto_connect.cpp`
- Modify: `host/CMakeLists.txt` (add `gui/auto_connect.cpp` to `droppix_gui`; add source + test to `droppix_gui_tests`)

**Interfaces:**
- Produces:
  - `struct droppix::AutoConnectCandidate { QString key; bool eligible = false; };`
  - `QList<QString> droppix::devicesToConnect(bool enabled, const QList<AutoConnectCandidate>& candidates, const QSet<QString>& activeKeys);`
- Consumed by Task 4 (`MainWindow::evaluateAutoConnect`).

- [ ] **Step 1: Write the failing tests**

Create `host/gui/tests/test_auto_connect.cpp`:

```cpp
#include "auto_connect.h"
#include <gtest/gtest.h>

using namespace droppix;

static AutoConnectCandidate cand(const QString& k, bool e) {
  AutoConnectCandidate c; c.key = k; c.eligible = e; return c;
}

TEST(AutoConnect, DisabledReturnsEmpty) {
  EXPECT_TRUE(devicesToConnect(false, {cand("usb:A", true)}, {}).isEmpty());
}

TEST(AutoConnect, IneligibleSkipped) {
  EXPECT_TRUE(devicesToConnect(true, {cand("net:1.2.3.4", false)}, {}).isEmpty());
}

TEST(AutoConnect, EligibleIncluded) {
  auto r = devicesToConnect(true, {cand("usb:A", true)}, {});
  ASSERT_EQ(r.size(), 1); EXPECT_EQ(r[0], "usb:A");
}

TEST(AutoConnect, ActiveKeySkipped) {
  QSet<QString> active = {QString("usb:A")};
  EXPECT_TRUE(devicesToConnect(true, {cand("usb:A", true)}, active).isEmpty());
}

TEST(AutoConnect, MixedSelectsOnlyEligibleInactive) {
  QList<AutoConnectCandidate> cs = {
    cand("usb:A", true), cand("net:B", false), cand("net:C", true), cand("usb:D", true)};
  QSet<QString> active = {QString("usb:D")};
  auto r = devicesToConnect(true, cs, active);
  ASSERT_EQ(r.size(), 2); EXPECT_EQ(r[0], "usb:A"); EXPECT_EQ(r[1], "net:C");
}
```

- [ ] **Step 2: Create the header**

Create `host/gui/auto_connect.h`:

```cpp
#pragma once
#include <QList>
#include <QSet>
#include <QString>

namespace droppix {

// A discovered device the host could auto-connect. `eligible` is precomputed by
// the caller: USB = app-bearing (always true); net = TXT id in the approved store.
struct AutoConnectCandidate {
  QString key;            // "usb:<serial>" or "net:<address>"
  bool eligible = false;
};

// Keys to auto-connect now: enabled AND eligible AND not already active.
QList<QString> devicesToConnect(bool enabled,
                                const QList<AutoConnectCandidate>& candidates,
                                const QSet<QString>& activeKeys);

}  // namespace droppix
```

- [ ] **Step 3: Create the implementation**

Create `host/gui/auto_connect.cpp`:

```cpp
#include "auto_connect.h"

namespace droppix {

QList<QString> devicesToConnect(bool enabled,
                                const QList<AutoConnectCandidate>& candidates,
                                const QSet<QString>& activeKeys) {
  QList<QString> out;
  if (!enabled) return out;
  for (const auto& c : candidates)
    if (c.eligible && !activeKeys.contains(c.key)) out.push_back(c.key);
  return out;
}

}  // namespace droppix
```

- [ ] **Step 4: Wire into CMake**

In `host/CMakeLists.txt`, add `gui/auto_connect.cpp` to the `droppix_gui` sources list (the block starting `gui/main.cpp`, near `gui/session_manager.cpp`).

In the `droppix_gui_tests` `add_executable(...)` block, add these two lines:

```cmake
    gui/tests/test_auto_connect.cpp
    gui/auto_connect.cpp
```

- [ ] **Step 5: Build and run the tests**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake . >/dev/null && cmake --build . --target droppix_gui_tests -j$(nproc) && ctest --output-on-failure -R AutoConnect'`
Expected: PASS — all 5 `AutoConnect.*` tests pass.

- [ ] **Step 6: Commit**

```bash
git add host/gui/auto_connect.h host/gui/auto_connect.cpp host/gui/tests/test_auto_connect.cpp host/CMakeLists.txt
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(auto-connect): pure AutoConnectPolicy + tests"
```

---

### Task 3: `SessionManager::keys()` (host)

**Files:**
- Modify: `host/gui/session_manager.h`
- Modify: `host/gui/session_manager.cpp`
- Test: `host/gui/tests/test_session_manager.cpp`

**Interfaces:**
- Produces: `QSet<QString> SessionManager::keys() const;` — the keys of all active sessions.
- Consumed by Task 4 (passed as `activeKeys` to `devicesToConnect`).

- [ ] **Step 1: Write the failing test**

Append to `host/gui/tests/test_session_manager.cpp`:

```cpp
TEST(SessionManager, KeysListsActiveKeys) {
  SessionManager m;
  EXPECT_TRUE(m.keys().isEmpty());
  m.add(mk("a", 27000)); m.add(mk("b", 27001));
  EXPECT_EQ(m.keys(), (QSet<QString>{QString("a"), QString("b")}));
  m.remove("a");
  EXPECT_EQ(m.keys(), (QSet<QString>{QString("b")}));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . --target droppix_gui_tests -j$(nproc)'`
Expected: FAIL to compile — `SessionManager` has no member `keys`.

- [ ] **Step 3: Declare `keys()`**

In `host/gui/session_manager.h`, add inside the class public section (near `usedPorts`):

```cpp
  QSet<QString> keys() const;   // keys of all active sessions
```

Ensure `#include <QSet>` is present at the top of the header (add it if missing).

- [ ] **Step 4: Implement `keys()`**

In `host/gui/session_manager.cpp`, add (match the iteration style already used by `usedPorts`):

```cpp
QSet<QString> SessionManager::keys() const {
  QSet<QString> k;
  for (const auto& s : sessions_) k.insert(s.key);
  return k;
}
```

(This mirrors `usedPorts()`, which iterates the same `QList<Session> sessions_` member.)

- [ ] **Step 5: Run the test to verify it passes**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . --target droppix_gui_tests -j$(nproc) && ctest --output-on-failure -R SessionManager'`
Expected: PASS — all `SessionManager.*` tests pass.

- [ ] **Step 6: Commit**

```bash
git add host/gui/session_manager.h host/gui/session_manager.cpp host/gui/tests/test_session_manager.cpp
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(auto-connect): SessionManager::keys()"
```

---

### Task 4: Auto-connect toggle setting (host)

**Files:**
- Modify: `host/gui/settings.h`
- Modify: `host/gui/profile_store.cpp`
- Test: `host/gui/tests/test_profile_store.cpp`
- Modify: `host/gui/settings_dialog.h`
- Modify: `host/gui/settings_dialog.cpp`

**Interfaces:**
- Produces: `Settings.autoConnect` (bool, default `true`), persisted as JSON key `"auto_connect"`, editable via a settings checkbox. Read by Task 4-wiring via `collectSettings().autoConnect`.

- [ ] **Step 1: Write the failing test**

In `host/gui/tests/test_profile_store.cpp`, in the `ProfileStore.SaveLoadRoundTrip` test, the pre-save `Settings` is named `s` and the post-load `Settings` is named `out`. Add one line to each.

After `s.port = 27123; ... s.touch = true;` (before `store.save(...)`):

```cpp
  s.autoConnect = false;   // default is true; prove it round-trips
```

After `EXPECT_TRUE(out.touch);` (in the post-load assertions):

```cpp
  EXPECT_FALSE(out.autoConnect);
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . --target droppix_gui_tests -j$(nproc)'`
Expected: FAIL to compile — `Settings` has no member `autoConnect`.

- [ ] **Step 3: Add the field**

In `host/gui/settings.h`, add after the `overlay` line:

```cpp
  bool autoConnect = true;  // auto-connect known tablets (USB + paired Wi-Fi) on discovery
```

- [ ] **Step 4: Persist it**

In `host/gui/profile_store.cpp`, next to the `o["overlay"] = s.overlay;` line add:

```cpp
  o["auto_connect"] = s.autoConnect;
```

Next to the `s.overlay = o["overlay"].toBool(s.overlay);` line add:

```cpp
  s.autoConnect = o["auto_connect"].toBool(s.autoConnect);
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . --target droppix_gui_tests -j$(nproc) && ctest --output-on-failure -R ProfileStore'`
Expected: PASS.

- [ ] **Step 6: Add the checkbox**

In `host/gui/settings_dialog.h`, add a member near `overlay_`:

```cpp
  QCheckBox* autoConnect_;
```

In `host/gui/settings_dialog.cpp`:
- Where the other checkboxes are constructed (near `overlay_ = new QCheckBox(...)`):

```cpp
  autoConnect_ = new QCheckBox("Auto-connect known monitors");
```
- Where checkboxes are added to the form (near `form->addRow("", overlay_);`):

```cpp
  form->addRow("", autoConnect_);
```
- In `load(const Settings& s)` (near `overlay_->setChecked(s.overlay);`):

```cpp
  autoConnect_->setChecked(s.autoConnect);
```
- In `store(Settings& s) const` (near `s.overlay = overlay_->isChecked();`):

```cpp
  s.autoConnect = autoConnect_->isChecked();
```

- [ ] **Step 7: Build the GUI**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . --target droppix_gui -j$(nproc)'`
Expected: builds clean.

- [ ] **Step 8: Commit**

```bash
git add host/gui/settings.h host/gui/profile_store.cpp host/gui/tests/test_profile_store.cpp host/gui/settings_dialog.h host/gui/settings_dialog.cpp
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(auto-connect): 'Auto-connect known monitors' toggle setting"
```

---

### Task 5: Wire auto-connect into MainWindow (host)

**Files:**
- Modify: `host/gui/main_window.h`
- Modify: `host/gui/main_window.cpp`

**Interfaces:**
- Consumes: `devicesToConnect(...)` (Task 2), `SessionManager::keys()` (Task 3), `Settings.autoConnect` (Task 4), `MdnsDevice.id` (Task 1), existing `approved_.isApproved(...)`, `sessions_.has/allocatePort`, `startSession(...)`, `adb_.usbConnect(...)`, `encode_wake(...)`.
- Produces: net list items carry the TXT id at `Qt::UserRole + 3`; a shared `connectDevice(...)` used by both manual and auto connect; `evaluateAutoConnect()` fired (debounced) after each discovery update.

- [ ] **Step 1: Surface the TXT id on network list items**

In `host/gui/main_window.cpp`, in `rebuildClientList()`, inside the `for (const auto& d : netDevices_)` loop, after `item->setData(Qt::UserRole + 2, (uint)d.port);` add:

```cpp
    item->setData(Qt::UserRole + 3, QString::fromStdString(d.id));   // for approved-store match
```

- [ ] **Step 2: Factor a shared `connectDevice` helper**

In `host/gui/main_window.h`, declare (near `onConnectToSelectedDevice`):

```cpp
  // Start a monitor for one specific device. quietIfBusy=true suppresses the
  // "already connected"/"limit reached" popups (used by auto-connect). Returns
  // true if a session was started.
  bool connectDevice(const QString& key, const QString& label, const QString& transport,
                     const QString& ident, quint16 wakePort, bool quietIfBusy);
  void evaluateAutoConnect();   // pick + start auto-connect sessions from discovery
```

In `host/gui/main_window.cpp`, replace the body of `onConnectToSelectedDevice()` (from `const int port = ...` through the `startSession(...)` call) with a call into the shared helper, and add the helper. The full replacement for `onConnectToSelectedDevice()`:

```cpp
void MainWindow::onConnectToSelectedDevice() {
  auto* item = devicesList_->currentItem();
  if (!item) return;
  const QString transport = item->data(Qt::UserRole).toString();
  const QString ident = item->data(Qt::UserRole + 1).toString();   // serial (usb) / addr (net)
  if (ident.isEmpty()) return;
  const QString key = transport + ":" + ident;
  const QString label = item->text();
  const quint16 wakePort = (quint16)item->data(Qt::UserRole + 2).toUInt();
  connectDevice(key, label, transport, ident, wakePort, /*quietIfBusy=*/false);
}

bool MainWindow::connectDevice(const QString& key, const QString& label, const QString& transport,
                               const QString& ident, quint16 wakePort, bool quietIfBusy) {
  if (sessions_.has(key)) {
    if (!quietIfBusy)
      QMessageBox::information(this, "Droppix", "That device already has an active monitor.");
    return false;
  }
  const int port = sessions_.allocatePort(collectSettings().port);
  if (port < 0) {
    if (!quietIfBusy) QMessageBox::information(this, "Droppix", "Monitor limit reached (4).");
    return false;
  }
  std::function<void()> direct;
  if (transport == "usb") {
    const QString serial = ident;
    direct = [this, serial, port]{ adb_.usbConnect(serial, port); };   // adb reverse + launch app
  } else {
    const QString addr = ident;
    direct = [this, addr, wakePort, port]{   // WAKE the tablet, which then dials this port
      auto bytes = encode_wake((uint16_t)port);
      QByteArray dg(reinterpret_cast<const char*>(bytes.data()), (int)bytes.size());
      pendingWakes_[addr] = QDateTime::currentMSecsSinceEpoch();
      QUdpSocket sock;
      sock.writeDatagram(dg, QHostAddress(addr), wakePort);
    };
  }
  startSession(key, label, transport, port, direct);
  return true;
}
```

- [ ] **Step 3: Add the debounce timer member**

In `host/gui/main_window.h`, add an include `#include <QTimer>` if absent, and a member near the discovery members:

```cpp
  QTimer autoConnectTimer_;   // debounces discovery bursts before auto-connecting
```

- [ ] **Step 4: Configure the timer and evaluation**

In the `MainWindow` constructor in `host/gui/main_window.cpp` (after the discovery signals are connected), add:

```cpp
  autoConnectTimer_.setSingleShot(true);
  autoConnectTimer_.setInterval(750);   // let a just-appeared tablet settle before WAKE
  connect(&autoConnectTimer_, &QTimer::timeout, this, &MainWindow::evaluateAutoConnect);
```

Add the evaluation method. `evaluateAutoConnect()` builds candidates from the last-discovered USB + net devices, computes eligibility, asks the policy, and connects each result:

```cpp
void MainWindow::evaluateAutoConnect() {
  if (!collectSettings().autoConnect) return;

  QList<AutoConnectCandidate> cands;
  for (const auto& c : usbClients_)
    cands.push_back({QString("usb:") + c.serial, /*eligible=*/true});
  for (const auto& d : netDevices_) {
    const QString id = QString::fromStdString(d.id);
    const QString addr = QString::fromStdString(d.address);
    cands.push_back({QString("net:") + addr, !id.isEmpty() && approved_.isApproved(id)});
  }

  const QList<QString> toConnect = devicesToConnect(true, cands, sessions_.keys());
  for (const QString& key : toConnect) {
    if (key.startsWith("usb:")) {
      const QString serial = key.mid(4);
      connectDevice(key, serial + " — USB", "usb", serial, 0, /*quietIfBusy=*/true);
    } else {  // "net:"
      const QString addr = key.mid(4);
      auto it = std::find_if(netDevices_.begin(), netDevices_.end(),
          [&](const MdnsDevice& d){ return QString::fromStdString(d.address) == addr; });
      if (it == netDevices_.end()) continue;
      const QString label = QString("%1 — %2").arg(QString::fromStdString(it->name), addr);
      connectDevice(key, label, "net", addr, it->port, /*quietIfBusy=*/true);
    }
  }
}
```

Add the includes needed at the top of `host/gui/main_window.cpp` if absent: `#include "auto_connect.h"` and `#include <algorithm>`.

- [ ] **Step 5: Trigger the debounce on discovery**

In `host/gui/main_window.cpp`, at the end of both `onDevicesChanged(...)` and `onUsbClientsChanged(...)` (each already calls `rebuildClientList()`), add:

```cpp
  autoConnectTimer_.start();   // (re)arm the debounced auto-connect evaluation
```

- [ ] **Step 6: Build the GUI and run the full host suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . -j$(nproc) && ctest --output-on-failure'`
Expected: builds clean; 100% tests pass.

- [ ] **Step 7: Commit**

```bash
git add host/gui/main_window.h host/gui/main_window.cpp
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(auto-connect): drive discovered devices through auto-connect on the host"
```

---

### Task 6: Tablet — advertise id and auto-accept a paired WAKE

**Files:**
- Create: `android/app/src/main/java/com/droppix/app/net/AutoAccept.kt`
- Create: `android/app/src/test/java/com/droppix/app/net/AutoAcceptTest.kt`
- Modify: `android/app/src/main/java/com/droppix/app/net/WakeService.kt`
- Modify: `android/app/src/main/java/com/droppix/app/ui/ConnectActivity.kt`

**Interfaces:**
- Produces: `com.droppix.app.net.shouldAutoAccept(isPaired: Boolean): Boolean`; the tablet's `_droppix-client._tcp` advertisement carries TXT `id=<DeviceIdentity.stableId>`.
- Consumes: existing `TlsTrust.isPaired(host)`, `DeviceIdentity.stableId(ctx)`.

- [ ] **Step 1: Write the failing test**

Create `android/app/src/test/java/com/droppix/app/net/AutoAcceptTest.kt`:

```kotlin
package com.droppix.app.net

import org.junit.Assert.*
import org.junit.Test

class AutoAcceptTest {
    @Test fun acceptsWhenPaired() { assertTrue(shouldAutoAccept(true)) }
    @Test fun promptsWhenNotPaired() { assertFalse(shouldAutoAccept(false)) }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle bash gradlew --no-daemon testDebugUnitTest --tests "com.droppix.app.net.AutoAcceptTest"'`
Expected: FAIL to compile — unresolved reference `shouldAutoAccept`.

- [ ] **Step 3: Create the helper**

Create `android/app/src/main/java/com/droppix/app/net/AutoAccept.kt`:

```kotlin
package com.droppix.app.net

/**
 * A tablet auto-accepts a host-initiated WAKE only from a host it has already
 * paired (pinned its TLS cert). An unpaired host still requires the confirm
 * dialog + PIN, so it can never be auto-connected. Named as a seam so the
 * decision is unit-testable apart from ConnectActivity.
 */
fun shouldAutoAccept(isPaired: Boolean): Boolean = isPaired
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle bash gradlew --no-daemon testDebugUnitTest --tests "com.droppix.app.net.AutoAcceptTest"'`
Expected: PASS — both tests pass.

- [ ] **Step 5: Advertise the device id in the TXT record**

In `android/app/src/main/java/com/droppix/app/net/WakeService.kt`, in the `NsdServiceInfo().apply { ... }` block, after `port = localPort`, add:

```kotlin
            setAttribute("id", DeviceIdentity.stableId(ctx))
```

- [ ] **Step 6: Auto-accept a paired host on WAKE**

In `android/app/src/main/java/com/droppix/app/ui/ConnectActivity.kt`, change the `onResume()` wake callback. Replace:

```kotlin
        wakeService.start { host, port -> showWakeConfirm(host, port) }
```

with:

```kotlin
        wakeService.start { host, port ->
            if (com.droppix.app.net.shouldAutoAccept(tlsTrust.isPaired(host))) {
                status.text = "Auto-connecting to $host..."
                connectTo(host, port)
            } else {
                showWakeConfirm(host, port)
            }
        }
```

- [ ] **Step 7: Build the debug APK and run the unit tests**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle bash gradlew --no-daemon assembleDebug testDebugUnitTest'`
Expected: BUILD SUCCESSFUL; all unit tests pass.

- [ ] **Step 8: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/net/AutoAccept.kt android/app/src/test/java/com/droppix/app/net/AutoAcceptTest.kt android/app/src/main/java/com/droppix/app/net/WakeService.kt android/app/src/main/java/com/droppix/app/ui/ConnectActivity.kt
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(auto-connect): advertise device id + auto-accept paired WAKE (client)"
```

---

### Task 7: End-to-end verification on hardware (manual)

**Files:** none (verification only).

- [ ] **Step 1: Rebuild + install the APK** on both tablets (per the project's build-apk flow) and rebuild the host GUI.
- [ ] **Step 2:** In host Settings, confirm **"Auto-connect known monitors"** is checked.
- [ ] **Step 3: First connect (establishes trust)** — manually Connect the Nexus over Wi-Fi once (pair with PIN) and the phone over USB once. Confirm both monitors come up.
- [ ] **Step 4: Auto-connect** — Stop both. Re-open the app on the Nexus and re-plug the phone. Expected: both monitors come up with NO per-device clicking on the host and NO confirm tap on the Nexus.
- [ ] **Step 5: Auto-reconnect** — drop one (walk the Nexus out of Wi-Fi / unplug the phone) and bring it back. Expected: it reconnects on reappear.
- [ ] **Step 6: Toggle off** — uncheck the toggle; disconnect a tablet and bring it back. Expected: it does NOT auto-connect (manual only); existing monitors stay up.

---

## Notes for the implementer

- After Task 6, if you run the whole host suite it should be **≥ the current 132 tests + the new AutoConnect/SessionManager/MdnsBrowse/ProfileStore cases**, all passing.
- `evaluateAutoConnect()` always passes `enabled=true` to `devicesToConnect` because it early-returns when the setting is off; the `enabled` parameter exists so the policy is fully testable in isolation.
- Do not add a separate "in-flight" guard — `startSession` registers the session in `sessions_` synchronously, so `sessions_.has(key)` (checked in `connectDevice` and via `sessions_.keys()` in the policy) already prevents double-connects.
