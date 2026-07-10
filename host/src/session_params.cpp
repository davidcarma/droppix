#include "session_params.h"
namespace droppix {
SessionParams select_session_params(uint32_t client_version, uint32_t hello_fps,
                                    uint8_t hello_audio, uint8_t hello_orientation,
                                    int default_fps, bool default_audio, int default_orientation) {
  if (client_version >= 4) {
    return { hello_fps > 0 ? static_cast<int>(hello_fps) : default_fps,
             hello_audio != 0,
             static_cast<int>(hello_orientation & 3) };
  }
  return { default_fps, default_audio, default_orientation };
}
}  // namespace droppix
