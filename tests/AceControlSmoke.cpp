#include "etherbeat/AceStepRequestCodec.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

bool has(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

template <typename Fn>
bool throws_invalid(Fn&& fn) {
    try {
        fn();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

} // namespace

int main() {
    using namespace etherbeat;

    const auto caps = ace_step_provider_capabilities();
    if (!has_capability(caps, ProviderCapability::TextToInstrumental) ||
        !has_capability(caps, ProviderCapability::Variation) ||
        !has_capability(caps, ProviderCapability::AudioToAudio) ||
        !has_capability(caps, ProviderCapability::ReplaceSection) ||
        !has_capability(caps, ProviderCapability::ReferenceAudio) ||
        !has_capability(caps, ProviderCapability::ControlRole) ||
        !has_capability(caps, ProviderCapability::TemporalControl) ||
        !has_capability(caps, ProviderCapability::DraftRole) ||
        !has_capability(caps, ProviderCapability::QualityRole)) {
        std::cerr << "ACE-Step live control capabilities are incomplete\n";
        return 1;
    }

    if (has_capability(caps, ProviderCapability::Extend) ||
        has_capability(caps, ProviderCapability::ComponentLocks) ||
        has_capability(caps, ProviderCapability::DrumConditioning) ||
        has_capability(caps, ProviderCapability::MelodyConditioning) ||
        has_capability(caps, ProviderCapability::HarmonyConditioning)) {
        std::cerr << "ACE-Step advertises an unimplemented precision capability\n";
        return 1;
    }

    GenerationRequest text;
    text.prompt = "haunted instrumental";
    text.duration_seconds = 20.0;
    text.seed = 1444;
    text.bpm = 68.0;
    text.key = "F# minor";
    const auto text_payload = build_ace_step_request_payload(text);
    if (text_payload.task_type != "text2music" ||
        !has(text_payload.json_body, "\"task_type\":\"text2music\"") ||
        has(text_payload.json_body, "src_audio_path")) {
        std::cerr << "ACE-Step text2music mapping is wrong\n";
        return 1;
    }

    GenerationRequest variation;
    variation.prompt = "keep the structure but make the texture colder";
    variation.mode = GenerationMode::Variation;
    variation.render_intent = RenderIntent::Control;
    variation.reference_audio = "control_source.wav";
    variation.control.reference_strength = 0.82;
    variation.mutation_amount = 0.30;
    const auto cover = build_ace_step_request_payload(variation);
    if (cover.task_type != "cover" ||
        !has(cover.json_body, "\"task_type\":\"cover\"") ||
        !has(cover.json_body, "src_audio_path") ||
        !has(cover.json_body, "control_source.wav") ||
        !has(cover.json_body, "\"audio_cover_strength\":0.820") ||
        !has(cover.json_body, "\"instruction\"")) {
        std::cerr << "ACE-Step Variation -> cover mapping is wrong\n";
        return 1;
    }

    GenerationRequest audio_to_audio = variation;
    audio_to_audio.mode = GenerationMode::AudioToAudio;
    const auto a2a = build_ace_step_request_payload(audio_to_audio);
    if (a2a.task_type != "cover") {
        std::cerr << "ACE-Step AudioToAudio -> cover mapping is wrong\n";
        return 1;
    }

    GenerationRequest repaint;
    repaint.prompt = "replace this region with a darker hook";
    repaint.mode = GenerationMode::ReplaceSection;
    repaint.render_intent = RenderIntent::Control;
    repaint.reference_audio = "source_song.wav";
    repaint.control.edit_start_seconds = 8.0;
    repaint.control.edit_end_seconds = 16.0;
    repaint.control.reference_strength = 0.90;
    const auto repaint_payload = build_ace_step_request_payload(repaint);
    if (repaint_payload.task_type != "repaint" ||
        !has(repaint_payload.json_body, "\"task_type\":\"repaint\"") ||
        !has(repaint_payload.json_body, "source_song.wav") ||
        !has(repaint_payload.json_body, "\"repainting_start\":8.000") ||
        !has(repaint_payload.json_body, "\"repainting_end\":16.000")) {
        std::cerr << "ACE-Step ReplaceSection -> repaint mapping is wrong\n";
        return 1;
    }

    GenerationRequest extend = variation;
    extend.mode = GenerationMode::Extend;
    if (!throws_invalid([&] { (void)build_ace_step_request_payload(extend); })) {
        std::cerr << "ACE-Step Extend should remain blocked until continuation semantics are validated\n";
        return 1;
    }

    GenerationRequest locked = variation;
    locked.control.locks = control_component(ControlComponent::Drums);
    if (!throws_invalid([&] { (void)build_ace_step_request_payload(locked); })) {
        std::cerr << "ACE-Step must reject unsupported component locks\n";
        return 1;
    }

    GenerationRequest symbolic = variation;
    symbolic.control.chord_progression = "F#m9 -> Dmaj7";
    if (!throws_invalid([&] { (void)build_ace_step_request_payload(symbolic); })) {
        std::cerr << "ACE-Step must reject unsupported symbolic chord conditioning\n";
        return 1;
    }

    return 0;
}
