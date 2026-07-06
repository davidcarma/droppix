# AOA M3: GUI USB Detection + Auto-Spawn Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The host GUI detects a plugged-in Android tablet over USB, lists it, and on Connect (or auto-start) spawns `droppix_stream --usb-aoa <serial>` via the existing pkexec-root path.

**Architecture:** A pure sysfs parser (`aoa_scan`) enumerates USB devices and keeps Android-vendor candidates; a `QObject` poller (`aoa_scanner`) emits them every 2 s; `MainWindow` shows "<product> — USB" rows and routes Connect through a new `--usb-aoa` branch in `build_command`; a serial store (`aoa_known_store`) gates auto-start to tablets already streamed once.

**Tech Stack:** C++17, Qt6 Widgets/Network, GoogleTest, sysfs (`/sys/bus/usb/devices`).

## Global Constraints

- **Host/GUI only.** No Android, wire-protocol, or TLS change. No new runtime dependency.
- **No udev rule.** Detection reads world-readable sysfs as the normal user; the streamer runs as root via pkexec.
- **Detection = known-Android-vendor allowlist OR accessory-mode**, excluding hubs/HID/storage. Never disturbs non-Android USB.
- **Auto-start on plug is gated:** fires only when the auto-connect toggle is ON **and** the device serial is in the known-store (populated on the first successful stream).
- **Serial is the AOA identity.** `--usb-aoa <serial>` matches the device's USB `iSerialNumber`.
- **Build/test environment:** edit sources in the CIFS tree at `/var/mnt/nas/Projects/Spacedesk for linux`; build off-mount inside the `droppix-dev` distrobox at `/home/Spinjitsudoomyt/droppix-build`. Build: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . -j"$(nproc)"'`. Test: append `&& ctest --output-on-failure -R <regex>`.
- Existing suites must stay green: host `droppix_tests` (147 tests) + `droppix_gui_tests`.

---

## File Structure

| File | Responsibility |
|---|---|
| `host/src/aoa_scan.{h,cpp}` (new) | Pure sysfs enumeration + vendor allowlist → `std::vector<AoaDevice>` |
| `host/tests/test_aoa_scan.cpp` (new) | Parser tests against a fake sysfs tree |
| `host/gui/aoa_known_store.{h,cpp}` (new) | Serial set at `~/.config/droppix_gui/known_aoa` |
| `host/gui/tests/test_aoa_known_store.cpp` (new) | Store round-trip test |
| `host/gui/args_builder.{h,cpp}` (mod) | `build_command` gains `usb_aoa_serial` → `--usb-aoa` |
| `host/tests/test_args_builder.cpp` (mod) | USB-AOA command tests |
| `host/gui/aoa_scanner.{h,cpp}` (new) | 2 s `QObject` poller → `clientsChanged(QList<AoaClient>)` |
| `host/gui/main_window.{h,cpp}` (mod) | Scanner wiring, list rows, Connect branch, auto-start |
| `host/CMakeLists.txt` (mod) | Register new sources + tests |

---

### Task 1: Pure sysfs scanner (`aoa_scan`)

**Files:**
- Create: `host/src/aoa_scan.h`, `host/src/aoa_scan.cpp`
- Create: `host/tests/test_aoa_scan.cpp`
- Modify: `host/CMakeLists.txt` (add `src/aoa_scan.cpp` to `droppix_core`; add `tests/test_aoa_scan.cpp` to `droppix_tests`)

**Interfaces:**
- Produces:
  - `struct droppix::AoaDevice { std::string serial; std::string product; uint16_t vendor_id=0; uint16_t product_id=0; bool accessory_mode=false; };`
  - `bool droppix::is_android_vendor(uint16_t vendor_id);`
  - `std::vector<droppix::AoaDevice> droppix::parse_usb_sysfs(const std::string& sysfs_root = "/sys/bus/usb/devices");`

- [ ] **Step 1: Write the failing test**

Create `host/tests/test_aoa_scan.cpp`:

```cpp
#include "aoa_scan.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace droppix;

namespace {
// Build one fake sysfs USB device dir <root>/<name> with the given attribute files.
// interfaces: list of bInterfaceClass hex strings, each becomes <name>:1.<i>/bInterfaceClass.
void make_dev(const fs::path& root, const std::string& name, const std::string& idVendor,
              const std::string& idProduct, const std::string& bDeviceClass,
              const std::string& serial, const std::string& product,
              const std::vector<std::string>& interfaces) {
  fs::create_directories(root / name);
  auto write = [&](const std::string& file, const std::string& val) {
    std::ofstream(root / name / file) << val << "\n";
  };
  write("idVendor", idVendor);
  write("idProduct", idProduct);
  write("bDeviceClass", bDeviceClass);
  if (!serial.empty()) write("serial", serial);
  if (!product.empty()) write("product", product);
  for (size_t i = 0; i < interfaces.size(); ++i) {
    const std::string iface = name + ":1." + std::to_string(i);
    fs::create_directories(root / iface);
    std::ofstream(root / iface / "bInterfaceClass") << interfaces[i] << "\n";
  }
}

// A unique temp sysfs root per test.
fs::path make_root(const std::string& tag) {
  fs::path root = fs::temp_directory_path() / ("droppix-aoa-scan-" + tag);
  fs::remove_all(root);
  fs::create_directories(root);
  return root;
}
}  // namespace

TEST(AoaScan, KnownVendorsRecognized) {
  EXPECT_TRUE(is_android_vendor(0x18d1));   // Google
  EXPECT_TRUE(is_android_vendor(0x04e8));   // Samsung
  EXPECT_TRUE(is_android_vendor(0x2717));   // Xiaomi
  EXPECT_FALSE(is_android_vendor(0x1d6b));  // Linux Foundation (root hub)
  EXPECT_FALSE(is_android_vendor(0x1234));  // unknown
}

TEST(AoaScan, ListsAndroidPhoneWithSerial) {
  auto root = make_root("phone");
  make_dev(root, "1-5", "18d1", "4ee7", "00", "R32D204ZH6J", "Nexus 10", {"ff"});
  auto devs = parse_usb_sysfs(root.string());
  ASSERT_EQ(devs.size(), 1u);
  EXPECT_EQ(devs[0].serial, "R32D204ZH6J");
  EXPECT_EQ(devs[0].product, "Nexus 10");
  EXPECT_EQ(devs[0].vendor_id, 0x18d1);
  EXPECT_FALSE(devs[0].accessory_mode);
}

TEST(AoaScan, ExcludesNonAndroidVendor) {
  auto root = make_root("nonandroid");
  make_dev(root, "1-5", "1234", "5678", "00", "SN1", "Widget", {"ff"});
  EXPECT_TRUE(parse_usb_sysfs(root.string()).empty());
}

TEST(AoaScan, ExcludesHub) {
  auto root = make_root("hub");
  make_dev(root, "usb1", "1d6b", "0002", "09", "0000:00:14.0", "xHCI Host Controller", {"09"});
  EXPECT_TRUE(parse_usb_sysfs(root.string()).empty());
}

TEST(AoaScan, ExcludesStorageOnlyEvenFromAndroidVendor) {
  auto root = make_root("ssd");
  // Samsung (0x04e8) also makes mass-storage; a storage-only device must not be listed.
  make_dev(root, "2-1", "04e8", "61f5", "00", "SSDSERIAL", "Portable SSD", {"08"});
  EXPECT_TRUE(parse_usb_sysfs(root.string()).empty());
}

TEST(AoaScan, DetectsAccessoryMode) {
  auto root = make_root("accessory");
  make_dev(root, "1-5", "18d1", "2d01", "00", "0000", "", {"ff", "ff"});
  auto devs = parse_usb_sysfs(root.string());
  ASSERT_EQ(devs.size(), 1u);
  EXPECT_TRUE(devs[0].accessory_mode);
  EXPECT_EQ(devs[0].product_id, 0x2d01);
}

TEST(AoaScan, ExcludesAndroidDeviceWithNoSerial) {
  auto root = make_root("noserial");
  make_dev(root, "1-5", "18d1", "4ee7", "00", "", "Nexus 10", {"ff"});
  EXPECT_TRUE(parse_usb_sysfs(root.string()).empty());
}

TEST(AoaScan, ReturnsMultipleDevicesSortedBySerial) {
  auto root = make_root("multi");
  make_dev(root, "1-5", "18d1", "4ee7", "00", "ZZZ", "Nexus 10", {"ff"});
  make_dev(root, "2-1", "2717", "ff40", "00", "AAA", "Mi Pad", {"ff"});
  auto devs = parse_usb_sysfs(root.string());
  ASSERT_EQ(devs.size(), 2u);
  EXPECT_EQ(devs[0].serial, "AAA");   // sorted
  EXPECT_EQ(devs[1].serial, "ZZZ");
}
```

- [ ] **Step 2: Add `aoa_scan.h`**

Create `host/src/aoa_scan.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace droppix {

// An AOA-capable Android device seen on the USB bus (read from sysfs).
struct AoaDevice {
  std::string serial;       // USB iSerialNumber (matches `--usb-aoa <serial>`)
  std::string product;      // sysfs "product" (display name); may be empty
  uint16_t vendor_id = 0;
  uint16_t product_id = 0;
  bool accessory_mode = false;   // idProduct is 0x2d00 / 0x2d01 (already an AOA accessory)
};

// True if vendor_id is a known Android OEM (the set adb's udev rules ship).
bool is_android_vendor(uint16_t vendor_id);

// Enumerate USB devices under `sysfs_root`, returning AOA candidates: a device is
// kept iff (is_android_vendor(vendor) OR accessory_mode) AND it is not a hub AND
// (accessory_mode OR it has at least one interface that is not HID/mass-storage).
// A device without a readable serial is dropped (can't be targeted). Missing or
// unreadable attribute files are treated as empty. Result is sorted by serial.
std::vector<AoaDevice> parse_usb_sysfs(const std::string& sysfs_root = "/sys/bus/usb/devices");

}  // namespace droppix
```

- [ ] **Step 3: Add `aoa_scan.cpp`**

Create `host/src/aoa_scan.cpp`:

```cpp
#include "aoa_scan.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_set>

namespace fs = std::filesystem;

namespace droppix {
namespace {

constexpr uint16_t kAccVid = 0x18d1;
constexpr uint16_t kAccPid0 = 0x2d00, kAccPid1 = 0x2d01;
constexpr int kClassHid = 0x03, kClassStorage = 0x08, kClassHub = 0x09;

// Read a sysfs attribute file and return its trimmed contents ("" if absent).
std::string read_attr(const fs::path& p) {
  std::ifstream f(p);
  if (!f) return "";
  std::string s;
  std::getline(f, s);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}

// Parse a hex string like "18d1" -> 0x18d1. Empty/garbage -> 0.
uint16_t hex16(const std::string& s) {
  if (s.empty()) return 0;
  try { return static_cast<uint16_t>(std::stoul(s, nullptr, 16)); }
  catch (...) { return 0; }
}

int dec_or_hex_class(const std::string& s) {   // sysfs classes are 2-digit hex
  if (s.empty()) return -1;
  try { return static_cast<int>(std::stoul(s, nullptr, 16)); }
  catch (...) { return -1; }
}

}  // namespace

bool is_android_vendor(uint16_t vendor_id) {
  // From android/sdk platform-tools' 51-android.rules (common OEM USB vendor IDs).
  static const std::unordered_set<uint16_t> kVendors = {
    0x18d1,  // Google
    0x04e8,  // Samsung
    0x2717,  // Xiaomi
    0x2a70,  // OnePlus / OPPO
    0x22b8,  // Motorola
    0x12d1,  // Huawei
    0x1004,  // LG
    0x0fce,  // Sony
    0x0b05,  // Asus
    0x0bb4,  // HTC
    0x1ebf,  // Realme (BBK)
    0x0489,  // Foxconn (some Nokia/Sharp)
    0x2916,  // Yota/Android
    0x109b,  // Hisense
    0x413c,  // Dell (some Android)
    0x0e8d,  // MediaTek (many generic Android)
    0x19d2,  // ZTE
  };
  return kVendors.count(vendor_id) != 0;
}

std::vector<AoaDevice> parse_usb_sysfs(const std::string& sysfs_root) {
  std::vector<AoaDevice> out;
  std::error_code ec;
  if (!fs::exists(sysfs_root, ec)) return out;

  // Pass 1: map each device name -> its interface classes (interface dirs contain ':').
  std::multimap<std::string, int> iface_classes;
  std::vector<std::string> device_names;
  for (const auto& entry : fs::directory_iterator(sysfs_root, ec)) {
    const std::string name = entry.path().filename().string();
    const auto colon = name.find(':');
    if (colon != std::string::npos) {
      const std::string dev = name.substr(0, colon);
      const int cls = dec_or_hex_class(read_attr(entry.path() / "bInterfaceClass"));
      if (cls >= 0) iface_classes.emplace(dev, cls);
    } else if (fs::exists(entry.path() / "idVendor", ec)) {
      device_names.push_back(name);
    }
  }

  // Pass 2: evaluate each device.
  for (const auto& name : device_names) {
    const fs::path dir = fs::path(sysfs_root) / name;
    const int devClass = dec_or_hex_class(read_attr(dir / "bDeviceClass"));
    if (devClass == kClassHub) continue;

    AoaDevice d;
    d.vendor_id = hex16(read_attr(dir / "idVendor"));
    d.product_id = hex16(read_attr(dir / "idProduct"));
    d.accessory_mode = (d.vendor_id == kAccVid &&
                        (d.product_id == kAccPid0 || d.product_id == kAccPid1));
    if (!d.accessory_mode && !is_android_vendor(d.vendor_id)) continue;

    d.serial = read_attr(dir / "serial");
    if (d.serial.empty()) continue;   // can't target --usb-aoa without a serial
    d.product = read_attr(dir / "product");

    if (!d.accessory_mode) {
      // Drop devices whose only interfaces are HID or mass-storage (keyboards,
      // drives) even from an Android vendor. Accessory-mode devices are always kept.
      auto range = iface_classes.equal_range(name);
      bool has_iface = range.first != range.second, only_hid_storage = has_iface;
      for (auto it = range.first; it != range.second; ++it)
        if (it->second != kClassHid && it->second != kClassStorage) only_hid_storage = false;
      if (only_hid_storage) continue;
    }
    out.push_back(std::move(d));
  }

  std::sort(out.begin(), out.end(),
            [](const AoaDevice& a, const AoaDevice& b) { return a.serial < b.serial; });
  return out;
}

}  // namespace droppix
```

- [ ] **Step 4: Register in CMake**

In `host/CMakeLists.txt`, add to the `droppix_core` source list (after line 43, `src/aoa_connect.cpp`):

```cmake
  src/aoa_scan.cpp
```

And add to the `droppix_tests` source list (after line 131, `tests/test_audio_sink.cpp`):

```cmake
  tests/test_aoa_scan.cpp
```

- [ ] **Step 5: Build and run the test**

Run:
```
distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . -j"$(nproc)" && ctest --output-on-failure -R AoaScan'
```
Expected: all `AoaScan.*` tests PASS; the full build succeeds.

- [ ] **Step 6: Commit**

```bash
git add host/src/aoa_scan.h host/src/aoa_scan.cpp host/tests/test_aoa_scan.cpp host/CMakeLists.txt
git commit -m "feat(aoa-m3): pure sysfs USB scanner (aoa_scan) + Android-vendor allowlist"
```

---

### Task 2: Known-serial store (`aoa_known_store`)

**Files:**
- Create: `host/gui/aoa_known_store.h`, `host/gui/aoa_known_store.cpp`
- Create: `host/gui/tests/test_aoa_known_store.cpp`
- Modify: `host/CMakeLists.txt` (add `gui/aoa_known_store.cpp` to `droppix_gui`; add the test + source to `droppix_gui_tests`)

**Interfaces:**
- Produces:
  - `class droppix::AoaKnownStore { explicit AoaKnownStore(QString dir); bool contains(const QString& serial) const; void add(const QString& serial); QStringList all() const; };`
  - Persists to `<dir>/known_aoa`, one serial per line (same shape as `ApprovedStore`).

- [ ] **Step 1: Write the failing test**

Create `host/gui/tests/test_aoa_known_store.cpp`:

```cpp
#include "aoa_known_store.h"
#include <gtest/gtest.h>
#include <QDir>
#include <QTemporaryDir>

using namespace droppix;

TEST(AoaKnownStore, AddContainsPersists) {
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  {
    AoaKnownStore s(tmp.path());
    EXPECT_FALSE(s.contains("R32D204ZH6J"));
    s.add("R32D204ZH6J");
    s.add("R32D204ZH6J");   // idempotent
    EXPECT_TRUE(s.contains("R32D204ZH6J"));
    EXPECT_EQ(s.all().size(), 1);
  }
  // A fresh store over the same dir reloads the persisted serial.
  AoaKnownStore reloaded(tmp.path());
  EXPECT_TRUE(reloaded.contains("R32D204ZH6J"));
}

TEST(AoaKnownStore, EmptyByDefault) {
  QTemporaryDir tmp;
  AoaKnownStore s(tmp.path());
  EXPECT_TRUE(s.all().isEmpty());
  EXPECT_FALSE(s.contains("anything"));
}
```

- [ ] **Step 2: Add `aoa_known_store.h`**

Create `host/gui/aoa_known_store.h`:

```cpp
#pragma once
#include <QSet>
#include <QString>
#include <QStringList>

namespace droppix {
// Remembers USB serials of tablets we've successfully streamed to over AOA,
// persisted to <dir>/known_aoa (one serial per line). Drives auto-start on plug.
class AoaKnownStore {
 public:
  explicit AoaKnownStore(QString dir);
  bool contains(const QString& serial) const;
  void add(const QString& serial);
  QStringList all() const;
 private:
  QString path() const;   // <dir>/known_aoa
  QString dir_;
  QSet<QString> serials_;
};
}  // namespace droppix
```

- [ ] **Step 3: Add `aoa_known_store.cpp`**

Create `host/gui/aoa_known_store.cpp`:

```cpp
#include "aoa_known_store.h"
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace droppix {

AoaKnownStore::AoaKnownStore(QString dir) : dir_(std::move(dir)) {
  QFile f(path());
  if (f.open(QIODevice::ReadOnly)) {
    QTextStream in(&f);
    while (!in.atEnd()) {
      const QString line = in.readLine().trimmed();
      if (!line.isEmpty()) serials_.insert(line);
    }
  }
}

QString AoaKnownStore::path() const { return dir_ + "/known_aoa"; }

bool AoaKnownStore::contains(const QString& serial) const { return serials_.contains(serial); }

void AoaKnownStore::add(const QString& serial) {
  if (serial.isEmpty() || serials_.contains(serial)) return;
  serials_.insert(serial);
  QDir().mkpath(dir_);
  QFile f(path());
  if (f.open(QIODevice::Append)) f.write((serial + "\n").toUtf8());
}

QStringList AoaKnownStore::all() const {
  QStringList out(serials_.begin(), serials_.end());
  out.sort();
  return out;
}

}  // namespace droppix
```

- [ ] **Step 4: Register in CMake**

In `host/CMakeLists.txt`, add to the `droppix_gui` `target_sources` block (after line 82, `gui/auto_connect.cpp`):

```cmake
    gui/aoa_known_store.cpp
```

And add to the `droppix_gui_tests` source list (after line 154, `gui/auto_connect.cpp`):

```cmake
    gui/tests/test_aoa_known_store.cpp
    gui/aoa_known_store.cpp
```

- [ ] **Step 5: Build and run the test**

Run:
```
distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . -j"$(nproc)" && ctest --output-on-failure -R AoaKnownStore'
```
Expected: both `AoaKnownStore.*` tests PASS.

- [ ] **Step 6: Commit**

```bash
git add host/gui/aoa_known_store.h host/gui/aoa_known_store.cpp host/gui/tests/test_aoa_known_store.cpp host/CMakeLists.txt
git commit -m "feat(aoa-m3): AoaKnownStore — remember tablets streamed over AOA"
```

---

### Task 3: `build_command` `--usb-aoa` support

**Files:**
- Modify: `host/gui/args_builder.h`, `host/gui/args_builder.cpp`
- Modify: `host/tests/test_args_builder.cpp`

**Interfaces:**
- Consumes: `droppix::Settings`, `droppix::Command` (unchanged).
- Produces: `Command build_command(const Settings& s, const std::string& stream_bin, int port = -1, const std::string& touch_name = "", const std::string& usb_aoa_serial = "");` — when `usb_aoa_serial` is non-empty, args contain `--usb-aoa <serial>` and omit `--tls`/`--cert`/`--key`; evdi flags are unchanged.

- [ ] **Step 1: Write the failing test**

Append to `host/tests/test_args_builder.cpp`:

```cpp
TEST(ArgsBuilder, UsbAoaAddsSerialAndOmitsTls) {
  droppix::Settings s;
  s.source = droppix::Settings::Source::Evdi;
  s.tls = true;
  s.certPath = "/tmp/cert.pem";
  s.keyPath = "/tmp/key.pem";
  auto c = droppix::build_command(s, "/opt/droppix_stream", 27001, "droppix-touch-27001",
                                  "R32D204ZH6J");
  // evdi -> pkexec wrapper; args include the binary first.
  EXPECT_EQ(c.program, "pkexec");
  auto has = [&](const std::string& flag) {
    return std::find(c.args.begin(), c.args.end(), flag) != c.args.end();
  };
  // --usb-aoa <serial> present, in order.
  auto it = std::find(c.args.begin(), c.args.end(), "--usb-aoa");
  ASSERT_NE(it, c.args.end());
  ASSERT_NE(std::next(it), c.args.end());
  EXPECT_EQ(*std::next(it), "R32D204ZH6J");
  // TLS omitted for the cable.
  EXPECT_FALSE(has("--tls"));
  EXPECT_FALSE(has("--cert"));
  // evdi flags still present.
  EXPECT_TRUE(has("--refresh"));
  EXPECT_TRUE(has("--width"));
}

TEST(ArgsBuilder, NoUsbAoaByDefault) {
  droppix::Settings s;
  s.source = droppix::Settings::Source::Evdi;
  s.tls = true;
  s.certPath = "/tmp/cert.pem";
  s.keyPath = "/tmp/key.pem";
  auto c = droppix::build_command(s, "/opt/droppix_stream", 27000, "droppix-touch");
  auto has = [&](const std::string& flag) {
    return std::find(c.args.begin(), c.args.end(), flag) != c.args.end();
  };
  EXPECT_FALSE(has("--usb-aoa"));
  EXPECT_TRUE(has("--tls"));   // unchanged behavior when no serial
}
```

Ensure `#include <algorithm>` is present at the top of `test_args_builder.cpp` (add it if missing).

- [ ] **Step 2: Run the test to verify it fails**

Run:
```
distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . -j"$(nproc)" 2>&1 | tail -5'
```
Expected: COMPILE FAILURE — `build_command` called with 5 args but declared with 4.

- [ ] **Step 3: Update the declaration**

In `host/gui/args_builder.h`, change the `build_command` signature (line 12-13) to:

```cpp
// port < 0 uses s.port; empty touch_name uses "droppix-touch". Multi-monitor passes a
// per-session port + a unique touch device name (droppix-touch-<port>). A non-empty
// usb_aoa_serial makes this a USB/AOA session: adds --usb-aoa <serial> and omits TLS
// (the cable is the trust boundary); each concurrent monitor targets its own serial.
Command build_command(const Settings& s, const std::string& stream_bin,
                      int port = -1, const std::string& touch_name = "",
                      const std::string& usb_aoa_serial = "");
```

- [ ] **Step 4: Update the implementation**

In `host/gui/args_builder.cpp`, change the signature (line 5-6) and add the flag. New signature line:

```cpp
Command build_command(const Settings& s, const std::string& stream_bin,
                      int port, const std::string& touch_name,
                      const std::string& usb_aoa_serial) {
```

Then replace the TLS block (lines 33-37) with a USB-AOA-aware version:

```cpp
  if (!usb_aoa_serial.empty()) {
    // USB/AOA session: stream over the cable, no TLS (physical trust). The streamer
    // internally skips listen/TLS when --usb-aoa is set; --port is still harmless.
    a.push_back("--usb-aoa"); a.push_back(usb_aoa_serial);
  } else if (s.tls && !s.certPath.empty()) {
    a.push_back("--tls");
    a.push_back("--cert"); a.push_back(s.certPath);
    a.push_back("--key");  a.push_back(s.keyPath);
  }
```

- [ ] **Step 5: Run the tests to verify they pass**

Run:
```
distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . -j"$(nproc)" && ctest --output-on-failure -R ArgsBuilder'
```
Expected: all `ArgsBuilder.*` tests PASS (new + existing).

- [ ] **Step 6: Commit**

```bash
git add host/gui/args_builder.h host/gui/args_builder.cpp host/tests/test_args_builder.cpp
git commit -m "feat(aoa-m3): build_command emits --usb-aoa <serial> (omits TLS on the cable)"
```

---

### Task 4: USB poller (`aoa_scanner`)

**Files:**
- Create: `host/gui/aoa_scanner.h`, `host/gui/aoa_scanner.cpp`
- Modify: `host/CMakeLists.txt` (add `gui/aoa_scanner.cpp` to `droppix_gui`)

**Interfaces:**
- Consumes: `droppix::parse_usb_sysfs` (Task 1).
- Produces:
  - `struct droppix::AoaClient { QString serial; QString product; bool accessoryMode = false; };` (with `Q_DECLARE_METATYPE`)
  - `class droppix::AoaScanner : public QObject { void start(); void stop(); bool available() const; signals: void clientsChanged(QList<droppix::AoaClient> clients); };`

- [ ] **Step 1: Add `aoa_scanner.h`**

Create `host/gui/aoa_scanner.h`:

```cpp
#pragma once
#include <QObject>
#include <QTimer>
#include <QList>
#include <QString>
#include <QMetaType>

namespace droppix {

// A plugged Android tablet discovered over USB (sysfs), streamable via AOA.
struct AoaClient {
  QString serial;
  QString product;
  bool accessoryMode = false;
};

// Polls /sys/bus/usb/devices every 2s via parse_usb_sysfs and emits the current
// AOA-capable device set. No root, no external tool. Mirrors TetherScanner.
class AoaScanner : public QObject {
  Q_OBJECT
 public:
  explicit AoaScanner(QObject* p = nullptr);
  bool available() const { return true; }
  void start();   // emit immediately, then every 2s
  void stop();
 signals:
  void clientsChanged(QList<droppix::AoaClient> clients);
 private:
  void tick();
  QTimer timer_;
};

}  // namespace droppix

Q_DECLARE_METATYPE(droppix::AoaClient)
```

- [ ] **Step 2: Add `aoa_scanner.cpp`**

Create `host/gui/aoa_scanner.cpp`:

```cpp
#include "aoa_scanner.h"
#include "aoa_scan.h"

namespace droppix {

AoaScanner::AoaScanner(QObject* p) : QObject(p) {
  timer_.setInterval(2000);
  connect(&timer_, &QTimer::timeout, this, &AoaScanner::tick);
}

void AoaScanner::start() { tick(); timer_.start(); }
void AoaScanner::stop()  { timer_.stop(); }

void AoaScanner::tick() {
  QList<AoaClient> clients;
  for (const auto& d : parse_usb_sysfs()) {
    AoaClient c;
    c.serial = QString::fromStdString(d.serial);
    c.product = QString::fromStdString(d.product);
    c.accessoryMode = d.accessory_mode;
    clients.push_back(c);
  }
  emit clientsChanged(clients);
}

}  // namespace droppix
```

- [ ] **Step 3: Register in CMake**

In `host/CMakeLists.txt`, add to the `droppix_gui` `target_sources` block (after the `gui/aoa_known_store.cpp` line added in Task 2), the scanner **and** the pure parser (the GUI target does not link `droppix_core`, so it compiles the `src/` file it needs, as it already does for `src/tether_discovery.cpp`):

```cmake
    gui/aoa_scanner.cpp
    src/aoa_scan.cpp
```

- [ ] **Step 4: Build to verify it compiles**

Run:
```
distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . --target droppix_gui -j"$(nproc)" 2>&1 | tail -5'
```
Expected: `droppix_gui` builds (AutoMOC processes the new `Q_OBJECT`). No unit test — it's a thin timer over the Task 1 parser, validated by the parser tests and the on-device check in Task 5.

- [ ] **Step 5: Commit**

```bash
git add host/gui/aoa_scanner.h host/gui/aoa_scanner.cpp host/CMakeLists.txt
git commit -m "feat(aoa-m3): AoaScanner — 2s sysfs poller emitting AOA-capable tablets"
```

---

### Task 5: MainWindow wiring (list, Connect, auto-start)

**Files:**
- Modify: `host/gui/main_window.h`, `host/gui/main_window.cpp`

**Interfaces:**
- Consumes: `AoaScanner`/`AoaClient` (Task 4), `AoaKnownStore` (Task 2), `build_command(..., usb_aoa_serial)` (Task 3), and existing `connectDevice`, `startSession`, `wireSession`, `rebuildClientList`, `evaluateAutoConnect`, `devicesToConnect`, `configDir()`.
- Produces: no new public interface — this task integrates the pieces.

**Design notes for the implementer:**
- The USB-AOA transport string is `"usb-aoa"`; the session key is `"usb-aoa:" + serial`; the list item stores the serial in both `Qt::UserRole+1` (ident) and `Qt::UserRole+3` (id/known-store key).
- **The GUI lists only normal-mode devices.** Skip `accessoryMode` clients in `rebuildClientList` (a device streaming over AOA re-enumerates into accessory mode with serial "0000"; showing it would duplicate an active session. A stuck accessory device is recovered by replugging — `aoa_connect` also resets one on the next connect).
- For a USB-AOA session there is **no wake datagram** (the accessory auto-launches the app) and **no pairing popup** (no PIN).

- [ ] **Step 1: Add members and the slot to `main_window.h`**

Add includes near the other scanner include (line 13, after `#include "tether_scanner.h"`):

```cpp
#include "aoa_scanner.h"
#include "aoa_known_store.h"
```

Add the slot declaration next to `onTetherClientsChanged` (line 49):

```cpp
  void onAoaClientsChanged(const QList<AoaClient>& clients);
```

Add members next to `tetherScanner_`/`tetherClients_` (lines 90-92):

```cpp
  AoaScanner aoaScanner_;
  QList<AoaClient> aoaClients_;   // last USB-discovered AOA tablets
```

Add the known-store member next to `approved_` (find the member declaration for `ApprovedStore approved_;` in `main_window.h` and add below it):

```cpp
  AoaKnownStore knownAoa_;
```

- [ ] **Step 2: Initialize `knownAoa_` in the constructor init list**

In `host/gui/main_window.cpp`, the constructor init list has `approved_(configDir()),` (line 127). Add after it:

```cpp
      knownAoa_(configDir()),
```

- [ ] **Step 3: Wire and start the scanner**

In the constructor body, after the tether wiring (line 131,
`connect(&tetherScanner_, &TetherScanner::clientsChanged, this, &MainWindow::onTetherClientsChanged);`) add:

```cpp
  connect(&aoaScanner_, &AoaScanner::clientsChanged, this, &MainWindow::onAoaClientsChanged);
```

And after `tetherScanner_.start();` (line 141) add:

```cpp
  aoaScanner_.start();   // poll USB for AOA-capable tablets while idle
```

- [ ] **Step 4: Add the `onAoaClientsChanged` slot**

In `host/gui/main_window.cpp`, after the `onTetherClientsChanged` definition, add:

```cpp
void MainWindow::onAoaClientsChanged(const QList<AoaClient>& clients) {
  aoaClients_ = clients;
  rebuildClientList();
  autoConnectTimer_.start();   // (re)arm the debounced auto-connect evaluation
}
```

- [ ] **Step 5: Add USB-AOA rows in `rebuildClientList`**

In `rebuildClientList()`, after the `tetherClients_` loop and before the `netDevices_` loop, add:

```cpp
  for (const auto& a : aoaClients_) {
    if (a.accessoryMode) continue;                    // owned-by-session / transient
    if (sessions_.has("usb-aoa:" + a.serial)) continue;  // already streaming this tablet
    const QString name = a.product.isEmpty() ? a.serial : a.product;
    auto* item = new QListWidgetItem(QString("%1 — USB").arg(name));
    item->setData(Qt::UserRole, "usb-aoa");
    item->setData(Qt::UserRole + 1, a.serial);        // ident = serial
    item->setData(Qt::UserRole + 2, (uint)0);         // no wake port
    item->setData(Qt::UserRole + 3, a.serial);        // id = serial (known-store key)
    devicesList_->addItem(item);
    if (item->text() == prevSelected) devicesList_->setCurrentItem(item);
  }
```

- [ ] **Step 6: Branch `connectDevice` for USB-AOA (no wake) and thread the serial through `startSession`**

In `connectDevice(...)`, replace the `direct` lambda definition (the block building the wake datagram) so USB-AOA sends no wake:

```cpp
  std::function<void()> direct;
  if (transport == "usb-aoa") {
    direct = []{};   // accessory auto-launches the app; nothing to send
  } else {
    const QString addr = ident;
    direct = [this, addr, wakePort, port]{
      auto bytes = encode_wake((uint16_t)port);
      QByteArray dg(reinterpret_cast<const char*>(bytes.data()), (int)bytes.size());
      pendingWakes_[addr] = QDateTime::currentMSecsSinceEpoch();
      QUdpSocket sock; sock.writeDatagram(dg, QHostAddress(addr), wakePort);
    };
  }
  startSession(key, label, transport, port, id, direct);
  return true;
```

(Delete the old `const QString addr = ident; auto direct = [...]{...};` lines this replaces.)

In `startSession(...)`, replace the `build_command` call:

```cpp
  const std::string tname = ("droppix-touch-" + QString::number(port)).toStdString();
  const std::string aoaSerial = (transport == "usb-aoa") ? id.toStdString() : std::string();
  Command cmd = build_command(s, streamBin_, port, tname, aoaSerial);
```

- [ ] **Step 7: Record the serial as "known" on first successful stream**

In `wireSession(...)`, update the `statsReceived` lambda so a connected USB-AOA session is remembered (it currently only sets `anyConnected_`):

```cpp
  connect(c, &StreamController::statsReceived, this, [this, key](const Stats& s){
    if (s.client_connected) {
      anyConnected_ = true;
      if (key.startsWith("usb-aoa:")) knownAoa_.add(key.mid(8));   // enable future auto-start
    }
    updateStatus();
  });
```

(Note the lambda now captures `key`, which `wireSession` already receives.)

- [ ] **Step 8: Suppress the pairing popup for USB-AOA**

In `showPairingPopup(const QString& ip)`, change the first guard (line
`if (ip == "127.0.0.1") return;`) to also skip the empty peer (AOA has no IP / no PIN):

```cpp
  if (ip.isEmpty() || ip == "127.0.0.1") return;   // USB / AOA / localhost: no pairing code
```

- [ ] **Step 9: Include USB-AOA candidates in `evaluateAutoConnect`**

Replace the body of `evaluateAutoConnect()` with a transport-generic version that adds USB-AOA candidates gated by the known-store:

```cpp
void MainWindow::evaluateAutoConnect() {
  if (!collectSettings().autoConnect) return;
  QList<AutoConnectCandidate> cands;
  for (int i = 0; i < devicesList_->count(); ++i) {
    auto* it = devicesList_->item(i);
    const QString transport = it->data(Qt::UserRole).toString();
    const QString ident = it->data(Qt::UserRole + 1).toString();
    const QString id = it->data(Qt::UserRole + 3).toString();
    const bool eligible = (transport == "usb-aoa")
        ? knownAoa_.contains(ident)                       // physical-trust: streamed before
        : (!id.isEmpty() && approved_.isApproved(id));    // net: paired/approved
    cands.push_back({transport + ":" + ident, id, eligible});
  }
  const QList<QString> toConnect = devicesToConnect(true, cands, sessions_.keys(), sessions_.ids());
  for (const QString& key : toConnect) {
    for (int i = 0; i < devicesList_->count(); ++i) {
      auto* it = devicesList_->item(i);
      const QString transport = it->data(Qt::UserRole).toString();
      const QString ident = it->data(Qt::UserRole + 1).toString();
      if (transport + ":" + ident != key) continue;
      connectDevice(key, it->text(), transport, ident,
                    (quint16)it->data(Qt::UserRole + 2).toUInt(),
                    it->data(Qt::UserRole + 3).toString(), /*quietIfBusy=*/true);
      break;
    }
  }
}
```

- [ ] **Step 10: Build the GUI**

Run:
```
distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . --target droppix_gui -j"$(nproc)" 2>&1 | tail -8'
```
Expected: `droppix_gui` builds clean.

- [ ] **Step 11: Run the full suite (no regressions)**

Run:
```
distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . -j"$(nproc)" && ctest --output-on-failure'
```
Expected: all tests pass (147 host + the new `AoaScan`/`AoaKnownStore`/`ArgsBuilder` cases + the gui suite).

- [ ] **Step 12: Commit**

```bash
git add host/gui/main_window.h host/gui/main_window.cpp
git commit -m "feat(aoa-m3): GUI lists plugged tablets, spawns --usb-aoa, auto-starts known ones"
```

- [ ] **Step 13: On-device manual validation (run by the operator, documented)**

1. Launch the GUI (`~/droppix-build/droppix_gui`), plug in the Nexus → a row **"Nexus 10 — USB"** appears in Available clients.
2. Select it → **Connect** → a real evdi extended monitor streams over the cable (the tablet auto-launches). Status dot goes green.
3. **Stop** the monitor, unplug, replug (with the auto-connect toggle on) → the monitor **auto-starts** (serial now in `~/.config/droppix_gui/known_aoa`).
4. Plug in a charge-only Android without the droppix app → the row appears; **Connect** shows a "waiting" monitor row; **Stop** ends it; no other USB device is disturbed.

---

## Self-Review

**Spec coverage:**
- Detection strategy (allowlist + accessory-mode, no root) → Task 1. ✓
- 2 s sysfs poller → Task 4. ✓
- `--usb-aoa` in `build_command`, TLS/wake omitted → Task 3 + Task 5 (connectDevice no-wake). ✓
- List "<product> — USB" rows, transport `usb-aoa`, ident=serial → Task 5 Step 5. ✓
- Connect branch + no pairing popup → Task 5 Steps 6, 8. ✓
- Record known on first CONFIG/connected → Task 5 Step 7 (first `statsReceived` with `client_connected`). ✓
- Auto-start gated by toggle + known-store → Task 5 Step 9. ✓
- No udev / no Android / no wire change → nothing in the plan touches them. ✓
- Tests: pure parser, known-store round-trip, build_command → Tasks 1, 2, 3. ✓

**Refinement vs. spec:** the spec said "always list a device already in accessory mode." The plan keeps accessory-mode *detection* in the pure parser (tested) but the GUI list **hides** accessory-mode rows, because a live AOA session re-enumerates the tablet into accessory mode and a visible "0000 — USB" row would duplicate the running session. Stuck-accessory recovery is via replug (+ `aoa_connect`'s reset). The spec's error-handling note is updated to match.

**Type consistency:** `build_command(..., usb_aoa_serial)` signature identical in `.h`/`.cpp`/tests/`startSession`. `AoaClient{serial,product,accessoryMode}` used consistently in scanner + `main_window`. `AutoConnectCandidate{key,id,eligible}` matches `auto_connect.h`. Session key format `"usb-aoa:"+serial` used identically in `rebuildClientList`, `connectDevice`, `wireSession` (`key.mid(8)`), and `evaluateAutoConnect`.

**Placeholder scan:** none — every step carries full code and exact commands.
