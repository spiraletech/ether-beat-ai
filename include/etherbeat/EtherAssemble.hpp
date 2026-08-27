#pragma once

#include "etherbeat/EtherArrangement.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace etherbeat {

struct PcmAudio {
    std::uint32_t sample_rate{0};
    std::uint16_t channels{0};
    std::vector<float> samples;

    [[nodiscard]] std::size_t frame_count() const noexcept {
        return channels == 0 ? 0u : samples.size() / channels;
    }

    [[nodiscard]] double duration_seconds() const noexcept {
        return sample_rate == 0 ? 0.0 : static_cast<double>(frame_count()) / static_cast<double>(sample_rate);
    }

    [[nodiscard]] bool valid() const noexcept {
        return sample_rate > 0 && channels > 0 && !samples.empty() && samples.size() % channels == 0;
    }
};

using AssemblyAudioDecoder = std::function<PcmAudio(const std::filesystem::path&)>;
using AssemblyPlaceholderResolver = std::function<std::optional<PcmAudio>(
    const ArrangementSlot& slot,
    double expected_duration_seconds)>;

struct AssembleOptions {
    // Used when adaptive_seams=false and retained as a deterministic fallback.
    double crossfade_seconds{0.020};
    bool require_all_placeholders{true};

    // EtherSeam V0.1 adapts each boundary independently between these limits.
    bool adaptive_seams{true};
    double min_crossfade_seconds{0.005};
    double max_crossfade_seconds{0.100};
    double severe_seam_score{0.82};
    bool reject_severe_seams{false};
};

struct AssembleSlotResult {
    std::string slot_id;
    std::string label;
    ArrangementOrigin origin{ArrangementOrigin::Source};
    bool generated{false};
    double input_duration_seconds{0.0};
    double output_start_seconds{0.0};
    double output_end_seconds{0.0};

    // The first slot has no incoming seam and therefore leaves these at zero.
    bool seam_analyzed{false};
    double seam_score{0.0};
    double rms_jump{0.0};
    double spectral_jump{0.0};
    double dc_jump{0.0};
    double transient_collision{0.0};
    double sample_jump{0.0};
    double applied_crossfade_seconds{0.0};
    bool severe_seam{false};
};

struct AssembleResult {
    std::filesystem::path audio_path;
    std::filesystem::path manifest_path;
    std::uint32_t sample_rate{0};
    std::uint16_t channels{0};
    double duration_seconds{0.0};

    // Legacy/fallback fixed crossfade requested by the caller.
    double crossfade_seconds{0.0};
    bool adaptive_seams{false};
    double average_seam_score{0.0};
    double max_seam_score{0.0};
    std::size_t severe_seam_count{0};

    std::vector<AssembleSlotResult> slots;

    [[nodiscard]] bool success() const noexcept {
        return !audio_path.empty() && sample_rate > 0 && channels > 0 && duration_seconds > 0.0;
    }
};

class EtherAssemble {
public:
    [[nodiscard]] AssembleResult render(
        const ArrangementPlan& plan,
        const std::filesystem::path& output_audio_path,
        const AssemblyAudioDecoder& decoder,
        const AssemblyPlaceholderResolver& placeholder_resolver = {},
        AssembleOptions options = {}) const;
};

[[nodiscard]] std::filesystem::path ether_assemble_manifest_path(
    const std::filesystem::path& audio_path);

} // namespace etherbeat
