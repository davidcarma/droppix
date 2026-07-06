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

TEST(AoaScan, ExcludesAndroidDeviceWithNoInterfaces) {
  auto root = make_root("nointerfaces");
  // Android vendor but zero discoverable interfaces: satisfies neither
  // accessory_mode nor "has a non-HID/storage interface" -> excluded (fail closed).
  make_dev(root, "1-5", "18d1", "4ee7", "00", "R32D204ZH6J", "Nexus 10", {});
  EXPECT_TRUE(parse_usb_sysfs(root.string()).empty());
}

TEST(AoaScan, DetectsAccessoryModePid2d00) {
  auto root = make_root("accessory2d00");
  make_dev(root, "1-5", "18d1", "2d00", "00", "0000", "", {"ff"});
  auto devs = parse_usb_sysfs(root.string());
  ASSERT_EQ(devs.size(), 1u);
  EXPECT_TRUE(devs[0].accessory_mode);
  EXPECT_EQ(devs[0].product_id, 0x2d00);
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
