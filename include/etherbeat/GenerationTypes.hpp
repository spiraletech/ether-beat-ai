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
    AudioToAudio
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
};

struct GenerationArtifact {
    std::filesystem::path audio_path;
    std::filesystem::path metadata_path;
    std::string backend_name;
    std::uint64_t resolved_seed{0};
};

} // namespace etherbeat
