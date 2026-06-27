#pragma once
#include <cstdint>
namespace droppix {
// ORIENTATION wire code -> KWin rotation in degrees. 0/1/2/3 => 0/90/180/270;
// any unknown code maps to 0 (landscape).
inline int orientation_degrees(uint8_t code) {
  switch (code) {
    case 1: return 90;
    case 2: return 180;
    case 3: return 270;
    default: return 0;
  }
}
}  // namespace droppix
