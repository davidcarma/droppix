#include "aoa_scan.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
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

int hex_class(const std::string& s) {   // sysfs classes are 2-digit hex
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
      const int cls = hex_class(read_attr(entry.path() / "bInterfaceClass"));
      if (cls >= 0) iface_classes.emplace(dev, cls);
    } else if (fs::exists(entry.path() / "idVendor", ec)) {
      device_names.push_back(name);
    }
  }

  // Pass 2: evaluate each device.
  for (const auto& name : device_names) {
    const fs::path dir = fs::path(sysfs_root) / name;
    const int devClass = hex_class(read_attr(dir / "bDeviceClass"));
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
      // Keep a non-accessory device only if it has >=1 interface that is not HID or
      // mass-storage (keyboards, drives). A device with no discoverable interfaces
      // fails closed and is excluded (the 2s poll retries once interfaces populate).
      // Accessory-mode devices are always kept regardless of interfaces.
      auto range = iface_classes.equal_range(name);
      bool has_iface = range.first != range.second, only_hid_storage = has_iface;
      for (auto it = range.first; it != range.second; ++it)
        if (it->second != kClassHid && it->second != kClassStorage) only_hid_storage = false;
      if (!has_iface || only_hid_storage) continue;
    }
    out.push_back(std::move(d));
  }

  std::sort(out.begin(), out.end(),
            [](const AoaDevice& a, const AoaDevice& b) { return a.serial < b.serial; });
  return out;
}

}  // namespace droppix
