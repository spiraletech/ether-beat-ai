#include "etherbeat/EtherControlUi.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace etherbeat;

    ControlUiInput input;
    input.source_audio = std::filesystem::path{"source.wav"};
    input.instruction = "make it colder and more degraded";
    input.reference_strength = 0.82;
    input.source_duration_seconds = 24.0;
    input.seed = 77;
    input.bpm = 68.0;
    input.key = "F# minor";

    const auto variation = make_control_ui_request(ControlUiAction::Variation, input);
    assert(variation.mode == GenerationMode::Variation);
    assert(variation.render_intent == RenderIntent::Control);
    assert(variation.reference_audio == input.source_audio);
    assert(std::abs(variation.control.reference_strength - 0.82) < 0.0001);
    assert(std::abs(variation.mutation_amount - 0.18) < 0.0001);
    assert(variation.duration_seconds == 24.0);
    assert(variation.seed == 77);

    input.edit_start_seconds = 8.0;
    input.edit_end_seconds = 16.0;
    const auto repaint = make_control_ui_request(ControlUiAction::ReplaceSection, input);
    assert(repaint.mode == GenerationMode::ReplaceSection);
    assert(repaint.control.edit_start_seconds == 8.0);
    assert(repaint.control.edit_end_seconds == 16.0);

    bool invalid_window_rejected = false;
    try {
        auto bad = input;
        bad.edit_start_seconds = 18.0;
        bad.edit_end_seconds = 12.0;
        (void)make_control_ui_request(ControlUiAction::ReplaceSection, bad);
    } catch (const std::invalid_argument&) {
        invalid_window_rejected = true;
    }
    assert(invalid_window_rejected);

    bool outside_track_rejected = false;
    try {
        auto bad = input;
        bad.edit_start_seconds = 20.0;
        bad.edit_end_seconds = 30.0;
        (void)make_control_ui_request(ControlUiAction::ReplaceSection, bad);
    } catch (const std::invalid_argument&) {
        outside_track_rejected = true;
    }
    assert(outside_track_rejected);

    std::cout << "EtherControl UI smoke passed\n";
    return 0;
}
