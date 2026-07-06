#pragma once
#include <memory>
#include <string>
#include "aoa_channel.h"

namespace droppix {

// Perform the Android Open Accessory handshake on a plugged-in Android and return a
// connected AoaChannel over its bulk endpoints, or nullptr if no device / handshake failed.
//
// `serial` (the device's USB iSerialNumber) selects which Android to use; empty falls back
// to a Google/Nexus device (VID 0x18d1) for single-device use. Needs raw USB access (the
// streamer already runs as root). Blocks up to ~5s for the accessory-mode re-enumeration.
// The returned AoaChannel owns the libusb context + handle.
std::unique_ptr<AoaChannel> aoa_connect(const std::string& serial);

}  // namespace droppix
