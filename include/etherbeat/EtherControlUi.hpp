#pragma once

#include "etherbeat/GenerationTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace etherbeat {

enum class ControlUiAction {
    Variation,
    ReplaceSection
};

struct ControlUiInput {
    std::filesystem::path source_audio;
    std::string instruction;
    double reference_strength{0.80};
    double edit_start_seconds{0.0};
    double edit_end_seconds{8.0};
    double source_duration_seconds{20.0};
    std::uint64_t seed{0};
    double bpm{0.0};
    std::string key;
};

[[nodiscard]] GenerationRequest make_control_ui_request(
    ControlUiAction action,
    const ControlUiInput& input);

} // namespace etherbeat
