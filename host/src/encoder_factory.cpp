#include "encoder_factory.h"

namespace droppix {

EncoderPref parse_encoder_pref(const std::string& s) {
  if (s == "nvenc") return EncoderPref::Nvenc;
  if (s == "vaapi") return EncoderPref::Vaapi;
  if (s == "software") return EncoderPref::Software;
  if (s == "auto") return EncoderPref::Auto;
  return EncoderPref::Auto;  // default for unknown
}

std::vector<EncoderBackend> select_encoder_order(EncoderPref pref) {
  switch (pref) {
    case EncoderPref::Auto:
      return {EncoderBackend::Nvenc, EncoderBackend::Vaapi, EncoderBackend::Software};
    case EncoderPref::Nvenc:
      return {EncoderBackend::Nvenc};
    case EncoderPref::Vaapi:
      return {EncoderBackend::Vaapi};
    case EncoderPref::Software:
      return {EncoderBackend::Software};
  }
  return {};  // unreachable
}

}  // namespace droppix
