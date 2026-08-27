#pragma once

#include "etherbeat/EtherAssemble.hpp"

#include <filesystem>

namespace etherbeat {

// Windows implementation uses Media Foundation and returns normalized
// interleaved float PCM suitable for EtherAssemble.
PcmAudio decode_audio_pcm_file(const std::filesystem::path& path);

} // namespace etherbeat
