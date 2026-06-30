#pragma once
#include <cstdint>
namespace droppix {
// Single-touch uinput TOUCHSCREEN (INPUT_PROP_DIRECT). KWin binds it to the droppix
// output (via the outputName DBus property), so the device's 0..65535 ABS range maps
// directly onto that monitor — the normalized touch coords are injected as-is.
class InputInjector {
 public:
  ~InputInjector();
  bool open();  // needs root /dev/uinput
  bool ok() const { return fd_ >= 0; }
  void inject(uint8_t action, uint16_t x_norm, uint16_t y_norm, uint16_t pressure);
 private:
  int fd_ = -1;
};
}  // namespace droppix
