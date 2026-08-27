#pragma once

#include "etherbeat/EtherDNA.hpp"

#include <filesystem>
#include <optional>

namespace etherbeat {

struct EtherCompareDelta {
    float energy{0.0f};
    float bass{0.0f};
    float mid{0.0f};
    float treble{0.0f};
    float beat_peak{0.0f};
    float low_end_weight{0.0f};
    float brightness{0.0f};
    float darkness{0.0f};
    float rhythmic_activity{0.0f};
    float spectral_center{0.0f};
    float spectrum_rmse{0.0f};
};

struct EtherCompareResult {
    std::filesystem::path a_audio;
    std::filesystem::path b_audio;
    EtherDNA a;
    EtherDNA b;
    EtherCompareDelta delta;
    float similarity{0.0f};
};

[[nodiscard]] EtherCompareResult compare_ether_dna(
    const EtherDNA& a,
    const EtherDNA& b);

[[nodiscard]] std::optional<EtherCompareResult> compare_audio_dna(
    const std::filesystem::path& a_audio,
    const std::filesystem::path& b_audio) noexcept;

} // namespace etherbeat
