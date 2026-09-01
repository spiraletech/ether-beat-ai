#pragma once

#include "etherbeat/GenerationTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace etherbeat {

struct ProductionIntentV1 {
    static constexpr std::uint32_t schema_version = 1;

    std::string prompt;
    double bpm{0.0};
    std::string key;
    double duration_seconds{10.0};
    std::uint64_t seed{0};
    double mutation_amount{0.35};
    std::filesystem::path reference_audio;

    // Normalized producer-facing dimensions. Values are expected in [0, 1].
    double drum_density{0.5};
    double bass_weight{0.5};
    double vocal_space{0.5};
    double texture_grit{0.5};
    double transient_density{0.5};

    // Components Spiral wants EtherBeat to preserve during a revision.
    ControlComponents locks{0};

    std::string chord_progression;
    std::vector<std::string> arrangement;
};

struct TrinityRequestV1 {
    static constexpr std::uint32_t schema_version = 1;

    std::string request_id;
    std::string project_id;
    ProductionIntentV1 intent;
};

struct TrinityCompileResultV1 {
    static constexpr std::uint32_t schema_version = 1;

    std::string request_id;
    std::string project_id;
    GenerationRequest generation_request;
};

class MusicTrinityAdapter {
public:
    [[nodiscard]] TrinityCompileResultV1 compile(const TrinityRequestV1& request) const;
};

} // namespace etherbeat
