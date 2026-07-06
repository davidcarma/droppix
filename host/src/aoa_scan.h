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
