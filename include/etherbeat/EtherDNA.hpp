#pragma once

#include "etherbeat/AudioAnalysis.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace etherbeat {

struct EtherDNA {
    std::string schema{"etherbeat.dna.v1"};
    std::filesystem::path source_audio;
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

    // Derived, renderer-agnostic traits. These are intentionally measurable
    // summaries rather than guessed genres or aesthetic labels.
    float low_end_weight{0.0f};
    float brightness{0.0f};
    float darkness{0.0f};
    float rhythmic_activity{0.0f};
    float spectral_center{0.0f};

    [[nodiscard]] std::string conditioning_summary() const;
};

[[nodiscard]] EtherDNA make_ether_dna(
    const std::filesystem::path& source_audio,
    const AudioAnalysis& analysis);

[[nodiscard]] std::filesystem::path ether_dna_sidecar_path(
    const std::filesystem::path& audio_path);

// Saves/loads EtherDNA as a portable UTF-8 JSON sidecar. Save returns false
// instead of throwing so analysis can still succeed on a read-only directory.
[[nodiscard]] bool save_ether_dna(
    const EtherDNA& dna,
    const std::filesystem::path& path) noexcept;

[[nodiscard]] std::optional<EtherDNA> load_ether_dna(
    const std::filesystem::path& path) noexcept;

[[nodiscard]] std::optional<EtherDNA> load_ether_dna_for_audio(
    const std::filesystem::path& audio_path) noexcept;

} // namespace etherbeat
