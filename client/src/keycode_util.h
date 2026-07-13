#pragma once
namespace droppix {
// X11 (xcb) scancodes are evdev + 8; Wayland scancodes are already evdev.
// Returns 0 for a scancode with no usable evdev code.
inline int scancode_to_evdev(int scancode, bool wayland) {
  int offset = wayland ? 0 : 8;
  if (scancode < offset + 1) return 0;   // would be <= 0
  return scancode - offset;
}
}
