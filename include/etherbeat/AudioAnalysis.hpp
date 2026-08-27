#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace etherbeat {

struct AudioAnalysis {
    bool ready{false};
    std::uint32_t sample_rate{0};
    std::uint32_t channels{0};
    std::uint64_t analyzed_windows{0};
    double duration_seconds{0.0};

    float energy{0.0f};
    float bass{0.0f};
    float mid{0.0f};
    float treble{0.0f};
    float beat_peak{0.0f};

    std::array<float, 32> spectrum{};
    std::string error;
};

AudioAnalysis analyze_audio_file(const std::filesystem::path& path);

} // namespace etherbeat
