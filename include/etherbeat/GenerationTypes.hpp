#pragma once

#include "etherbeat/ProviderTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace etherbeat {

enum class GenerationMode {
    TextToInstrumental,
    Variation,
    Extend,
    AudioToAudio,
    ReplaceSection
};

enum class ControlComponent : std::uint32_t {
    None        = 0,
    Drums       = 1u << 0,
    Bass        = 1u << 1,
    Melody      = 1u << 2,
    Harmony     = 1u << 3,
    Texture     = 1u << 4,
    Arrangement = 1u << 5
};

using ControlComponents = std::uint32_t;

[[nodiscard]] constexpr ControlComponents control_component(ControlComponent value) noexcept {
    return static_cast<ControlComponents>(value);
}

[[nodiscard]] constexpr ControlComponents operator|(ControlComponent a, ControlComponent b) noexcept {
    return control_component(a) | control_component(b);
}

[[nodiscard]] constexpr ControlComponents operator|(ControlComponents a, ControlComponent b) noexcept {
    return a | control_component(b);
}

[[nodiscard]] constexpr bool has_control_component(ControlComponents value, ControlComponent flag) noexcept {
    return (value & control_component(flag)) != 0;
}

struct ControlSpec {
    ControlComponents locks{0};
    double reference_strength{0.80};
    double edit_start_seconds{-1.0};
    double edit_end_seconds{-1.0};
    std::filesystem::path drum_reference;
    std::filesystem::path melody_reference;
    std::string chord_progression;
};

struct GenerationRequest {
    std::string prompt;
    GenerationMode mode{GenerationMode::TextToInstrumental};
    RenderIntent render_intent{RenderIntent::Auto};
    std::uint64_t seed{0};
    double duration_seconds{10.0};
    double bpm{0.0};
    std::string key;
    double mutation_amount{0.35};
    std::filesystem::path reference_audio;
    ControlSpec control{};
};

struct GenerationArtifact {
    std::filesystem::path audio_path;
    std::filesystem::path metadata_path;
    std::string backend_name;
    std::uint64_t resolved_seed{0};
};

} // namespace etherbeat
