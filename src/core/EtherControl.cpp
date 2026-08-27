#include "etherbeat/EtherControl.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace etherbeat {
namespace {

std::string mode_name(GenerationMode mode) {
    switch (mode) {
    case GenerationMode::TextToInstrumental: return "text-to-instrumental";
    case GenerationMode::Variation: return "variation";
    case GenerationMode::Extend: return "extend";
    case GenerationMode::AudioToAudio: return "audio-to-audio";
    case GenerationMode::ReplaceSection: return "replace-section";
    }
    return "unknown";
}

void add_lock(
    const GenerationRequest& request,
    ControlComponent component,
    const char* name,
    std::vector<std::string>& names) {

    if (has_control_component(request.control.locks, component)) {
        names.emplace_back(name);
    }
}

std::string join(const std::vector<std::string>& values) {
    if (values.empty()) return "none";
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << ", ";
        out << values[i];
    }
    return out.str();
}

bool has_edit_window(const ControlSpec& control) {
    return control.edit_start_seconds >= 0.0 && control.edit_end_seconds >= 0.0;
}

std::string filename_or_path(const std::filesystem::path& value) {
    if (value.empty()) return {};
    const auto name = value.filename();
    const auto bytes = (name.empty() ? value : name).u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace

ControlPlan EtherControl::compile(const GenerationRequest& request) const {
    if (request.mode == GenerationMode::TextToInstrumental) {
        throw std::invalid_argument("EtherControl only compiles Variation, Extend, Audio-to-Audio, or Replace Section jobs");
    }
    if (request.prompt.empty()) {
        throw std::invalid_argument("EtherControl requires a producer instruction");
    }
    if (request.reference_audio.empty()) {
        throw std::invalid_argument("EtherControl requires reference audio for every control job");
    }
    if (request.mutation_amount < 0.0 || request.mutation_amount > 1.0) {
        throw std::invalid_argument("EtherControl mutation amount must be between 0 and 1");
    }
    if (request.control.reference_strength < 0.0 || request.control.reference_strength > 1.0) {
        throw std::invalid_argument("EtherControl reference strength must be between 0 and 1");
    }

    const bool has_start = request.control.edit_start_seconds >= 0.0;
    const bool has_end = request.control.edit_end_seconds >= 0.0;
    if (has_start != has_end) {
        throw std::invalid_argument("EtherControl edit windows require both start and end times");
    }
    if (has_start && request.control.edit_end_seconds <= request.control.edit_start_seconds) {
        throw std::invalid_argument("EtherControl edit window end must be greater than start");
    }
    if (request.mode == GenerationMode::ReplaceSection && !has_edit_window(request.control)) {
        throw std::invalid_argument("Replace Section requires an explicit edit window");
    }

    ControlPlan plan;
    plan.request = request;
    plan.request.render_intent = RenderIntent::Control;

    add_lock(request, ControlComponent::Drums, "drums", plan.locked_components);
    add_lock(request, ControlComponent::Bass, "bass", plan.locked_components);
    add_lock(request, ControlComponent::Melody, "melody", plan.locked_components);
    add_lock(request, ControlComponent::Harmony, "harmony", plan.locked_components);
    add_lock(request, ControlComponent::Texture, "texture", plan.locked_components);
    add_lock(request, ControlComponent::Arrangement, "arrangement", plan.locked_components);

    std::ostringstream summary;
    summary << mode_name(request.mode)
            << " // reference " << std::fixed << std::setprecision(0)
            << request.control.reference_strength * 100.0 << "%"
            << " // mutation " << request.mutation_amount * 100.0 << "%"
            << " // locks " << join(plan.locked_components);
    if (has_edit_window(request.control)) {
        summary << " // window " << std::setprecision(2)
                << request.control.edit_start_seconds << "-"
                << request.control.edit_end_seconds << " sec";
    }
    plan.summary = summary.str();

    // Keep compilation idempotent if a caller explicitly runs EtherControl before ModelRouter.
    if (plan.request.prompt.find("Control blueprint:") == std::string::npos) {
        std::ostringstream prompt;
        prompt << request.prompt
               << ". Control blueprint: mode " << mode_name(request.mode)
               << "; reference strength " << std::fixed << std::setprecision(2)
               << request.control.reference_strength
               << "; mutation amount " << request.mutation_amount
               << "; locked components " << join(plan.locked_components) << ".";

        if (has_edit_window(request.control)) {
            prompt << " Only alter the temporal window from "
                   << request.control.edit_start_seconds << " to "
                   << request.control.edit_end_seconds
                   << " seconds; preserve continuity at both boundaries.";
        }
        if (!request.control.drum_reference.empty()) {
            prompt << " Drum conditioning reference: "
                   << filename_or_path(request.control.drum_reference) << ".";
        }
        if (!request.control.melody_reference.empty()) {
            prompt << " Melody conditioning reference: "
                   << filename_or_path(request.control.melody_reference) << ".";
        }
        if (!request.control.chord_progression.empty()) {
            prompt << " Harmony conditioning: " << request.control.chord_progression << ".";
        }
        if (!plan.locked_components.empty()) {
            prompt << " Locked components are preservation constraints, not suggestions; change only unlocked musical dimensions.";
        }

        plan.request.prompt = prompt.str();
    }

    return plan;
}

} // namespace etherbeat
