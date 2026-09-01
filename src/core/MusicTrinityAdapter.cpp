#include "etherbeat/MusicTrinityAdapter.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace etherbeat {
namespace {

double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

std::string build_renderer_prompt(const ProductionIntentV1& intent) {
    std::ostringstream out;
    out << intent.prompt;
    out << "\n[MusicTrinity:v1";
    out << " drum_density=" << std::fixed << std::setprecision(2) << clamp01(intent.drum_density);
    out << " bass_weight=" << clamp01(intent.bass_weight);
    out << " vocal_space=" << clamp01(intent.vocal_space);
    out << " texture_grit=" << clamp01(intent.texture_grit);
    out << " transient_density=" << clamp01(intent.transient_density);

    if (!intent.arrangement.empty()) {
        out << " arrangement=";
        for (std::size_t i = 0; i < intent.arrangement.size(); ++i) {
            if (i != 0) {
                out << '/';
            }
            out << intent.arrangement[i];
        }
    }

    out << ']';
    return out.str();
}

} // namespace

TrinityCompileResultV1 MusicTrinityAdapter::compile(const TrinityRequestV1& request) const {
    if (request.request_id.empty()) {
        throw std::invalid_argument("MusicTrinity request_id must not be empty");
    }

    if (request.intent.prompt.empty()) {
        throw std::invalid_argument("MusicTrinity production prompt must not be empty");
    }

    GenerationRequest generation;
    generation.prompt = build_renderer_prompt(request.intent);
    generation.mode = request.intent.reference_audio.empty()
        ? GenerationMode::TextToInstrumental
        : GenerationMode::Variation;
    generation.seed = request.intent.seed;
    generation.duration_seconds = request.intent.duration_seconds;
    generation.bpm = request.intent.bpm;
    generation.key = request.intent.key;
    generation.mutation_amount = clamp01(request.intent.mutation_amount);
    generation.reference_audio = request.intent.reference_audio;
    generation.control.locks = request.intent.locks;
    generation.control.chord_progression = request.intent.chord_progression;

    TrinityCompileResultV1 result;
    result.request_id = request.request_id;
    result.project_id = request.project_id;
    result.generation_request = std::move(generation);
    return result;
}

} // namespace etherbeat
