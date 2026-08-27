#include "etherbeat/EtherControlUi.hpp"

#include <algorithm>
#include <stdexcept>

namespace etherbeat {

GenerationRequest make_control_ui_request(
    ControlUiAction action,
    const ControlUiInput& input) {

    if (input.source_audio.empty()) {
        throw std::invalid_argument("EtherControl UI requires a loaded source track");
    }
    if (input.instruction.empty()) {
        throw std::invalid_argument("EtherControl UI requires a producer instruction");
    }
    if (input.reference_strength < 0.0 || input.reference_strength > 1.0) {
        throw std::invalid_argument("Reference strength must be between 0 and 1");
    }

    GenerationRequest request;
    request.prompt = input.instruction;
    request.reference_audio = input.source_audio;
    request.render_intent = RenderIntent::Control;
    request.seed = input.seed;
    request.duration_seconds = std::clamp(input.source_duration_seconds, 10.0, 600.0);
    request.bpm = input.bpm;
    request.key = input.key;
    request.control.reference_strength = input.reference_strength;
    request.mutation_amount = std::clamp(1.0 - input.reference_strength, 0.05, 0.95);

    switch (action) {
    case ControlUiAction::Variation:
        request.mode = GenerationMode::Variation;
        break;
    case ControlUiAction::ReplaceSection:
        if (input.edit_end_seconds <= input.edit_start_seconds) {
            throw std::invalid_argument("Replace Section end time must be later than start time");
        }
        if (input.edit_start_seconds < 0.0 || input.edit_end_seconds > request.duration_seconds) {
            throw std::invalid_argument("Replace Section window must stay inside the loaded track duration");
        }
        request.mode = GenerationMode::ReplaceSection;
        request.control.edit_start_seconds = input.edit_start_seconds;
        request.control.edit_end_seconds = input.edit_end_seconds;
        break;
    }

    return request;
}

} // namespace etherbeat
