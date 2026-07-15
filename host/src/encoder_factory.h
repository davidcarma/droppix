#pragma once

#include <memory>
#include <string>
#include <vector>
#include "encoder.h"

namespace droppix {

enum class EncoderPref { Auto, Nvenc, Vaapi, Software };
enum class EncoderBackend { Nvenc, Vaapi, Software };

EncoderPref parse_encoder_pref(const std::string& s);
std::vector<EncoderBackend> select_encoder_order(EncoderPref pref);

// Declaration only; implemented in Task 5
std::unique_ptr<Encoder> make_encoder(EncoderPref pref);

}  // namespace droppix
