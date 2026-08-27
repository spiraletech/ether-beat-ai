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
    double crossfade_seconds{0.020};
    bool require_all_placeholders{true};
};

struct AssembleSlotResult {
    std::string slot_id;
    std::string label;
    ArrangementOrigin origin{ArrangementOrigin::Source};
    bool generated{false};
    double input_duration_seconds{0.0};
    double output_start_seconds{0.0};
    double output_end_seconds{0.0};
};

struct AssembleResult {
    std::filesystem::path audio_path;
    std::filesystem::path manifest_path;
    std::uint32_t sample_rate{0};
    std::uint16_t channels{0};
    double duration_seconds{0.0};
    double crossfade_seconds{0.0};
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
