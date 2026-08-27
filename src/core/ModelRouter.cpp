#include "etherbeat/ModelRouter.hpp"
#include "etherbeat/MockWaveBackend.hpp"
#include "etherbeat/EtherComposer.hpp"
#include "etherbeat/EtherControl.hpp"
#include "etherbeat/EtherVersions.hpp"
#ifdef _WIN32
#include "etherbeat/ManagedAceStepBackend.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace etherbeat {
namespace {

ProviderCapability mode_capability(GenerationMode mode) {
    switch (mode) {
    case GenerationMode::TextToInstrumental: return ProviderCapability::TextToInstrumental;
    case GenerationMode::Variation: return ProviderCapability::Variation;
    case GenerationMode::Extend: return ProviderCapability::Extend;
    case GenerationMode::AudioToAudio: return ProviderCapability::AudioToAudio;
    case GenerationMode::ReplaceSection: return ProviderCapability::ReplaceSection;
    }
    return ProviderCapability::None;
}

ProviderCapability role_capability(RenderIntent intent) {
    switch (intent) {
    case RenderIntent::Draft: return ProviderCapability::DraftRole;
    case RenderIntent::Quality: return ProviderCapability::QualityRole;
    case RenderIntent::Control: return ProviderCapability::ControlRole;
    case RenderIntent::Vocal: return ProviderCapability::VocalRole;
    case RenderIntent::Auto: break;
    }
    return ProviderCapability::None;
}

bool is_control_mode(GenerationMode mode) {
    return mode != GenerationMode::TextToInstrumental;
}

RenderIntent resolve_intent(const GenerationRequest& request) {
    if (request.render_intent != RenderIntent::Auto) return request.render_intent;

    switch (request.mode) {
    case GenerationMode::TextToInstrumental:
        return RenderIntent::Quality;
    case GenerationMode::Variation:
    case GenerationMode::Extend:
    case GenerationMode::AudioToAudio:
    case GenerationMode::ReplaceSection:
        return RenderIntent::Control;
    }
    return RenderIntent::Quality;
}

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

bool needs_temporal_control(const GenerationRequest& request) {
    return request.mode == GenerationMode::ReplaceSection ||
        request.control.edit_start_seconds >= 0.0 ||
        request.control.edit_end_seconds >= 0.0;
}

bool provider_supports_control_details(
    ProviderCapabilities caps,
    const GenerationRequest& request) {

    if (request.control.locks != 0 &&
        !has_capability(caps, ProviderCapability::ComponentLocks)) return false;
    if (!request.control.drum_reference.empty() &&
        !has_capability(caps, ProviderCapability::DrumConditioning)) return false;
    if (!request.control.melody_reference.empty() &&
        !has_capability(caps, ProviderCapability::MelodyConditioning)) return false;
    if (!request.control.chord_progression.empty() &&
        !has_capability(caps, ProviderCapability::HarmonyConditioning)) return false;
    if (needs_temporal_control(request) &&
        !has_capability(caps, ProviderCapability::TemporalControl)) return false;
    return true;
}

std::filesystem::path version_library_root(const std::filesystem::path& output_directory) {
    auto cursor = output_directory.lexically_normal();
    for (;;) {
        auto name = cursor.filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == "library") return cursor;
        const auto parent = cursor.parent_path();
        if (parent.empty() || parent == cursor) break;
        cursor = parent;
    }
    return output_directory;
}

void persist_control_lineage(
    const GenerationRequest& request,
    const GenerationArtifact& artifact,
    const std::filesystem::path& output_directory) {

    if (!is_control_mode(request.mode) || request.reference_audio.empty() || artifact.audio_path.empty()) {
        return;
    }

    // Audio generation already succeeded. A metadata I/O failure must never destroy
    // a usable render, so lineage persistence is best-effort at this boundary.
    try {
        EtherVersions versions(version_library_root(output_directory));
        versions.register_child(
            request.reference_audio,
            artifact.audio_path,
            mode_name(request.mode),
            request.prompt);
    } catch (...) {
        // The audio artifact remains authoritative; a later library repair pass can
        // reconstruct or re-register lineage from generation metadata.
    }
}

} // namespace

ModelRouter::ModelRouter(std::unique_ptr<IModelBackend> backend, int priority) {
    add_provider(std::move(backend), priority);
}

void ModelRouter::add_provider(std::unique_ptr<IModelBackend> backend, int priority) {
    if (!backend) throw std::invalid_argument("ModelRouter cannot register a null provider");
    providers_.push_back(ProviderSlot{std::move(backend), priority});
}

std::size_t ModelRouter::provider_count() const noexcept {
    return providers_.size();
}

std::vector<ProviderInfo> ModelRouter::providers() const {
    std::vector<ProviderInfo> result;
    result.reserve(providers_.size());
    for (const auto& slot : providers_) {
        result.push_back(ProviderInfo{
            .name = std::string{slot.backend->name()},
            .capabilities = slot.backend->capabilities(),
            .priority = slot.priority
        });
    }
    return result;
}

const IModelBackend& ModelRouter::backend() const {
    if (providers_.empty()) throw std::runtime_error("ModelRouter has no providers registered");
    return *providers_.front().backend;
}

const ModelRouter::ProviderSlot& ModelRouter::select_provider(
    const GenerationRequest& request,
    RenderIntent resolved_intent) const {

    if (providers_.empty()) throw std::runtime_error("EtherBeat has no model providers registered");

    const ProviderCapability required_mode = mode_capability(request.mode);
    const ProviderCapability required_role = role_capability(resolved_intent);
    const bool needs_reference = !request.reference_audio.empty();

    const ProviderSlot* best = nullptr;
    int best_score = std::numeric_limits<int>::min();

    for (const auto& slot : providers_) {
        const ProviderCapabilities caps = slot.backend->capabilities();
        if (!has_capability(caps, required_mode)) continue;
        if (required_role != ProviderCapability::None && !has_capability(caps, required_role)) continue;
        if (needs_reference && !has_capability(caps, ProviderCapability::ReferenceAudio)) continue;
        if (!provider_supports_control_details(caps, request)) continue;

        int score = slot.priority;
        if (has_capability(caps, required_role)) score += 1000;
        if (needs_reference && has_capability(caps, ProviderCapability::ReferenceAudio)) score += 100;
        if (has_capability(caps, ProviderCapability::LocalRuntime)) score += 10;

        if (!best || score > best_score) {
            best = &slot;
            best_score = score;
        }
    }

    if (!best) {
        std::ostringstream error;
        error << "No EtherBeat provider supports " << mode_name(request.mode)
              << " as a " << render_intent_name(resolved_intent) << " job";
        if (needs_reference) error << " with reference audio";
        if (request.control.locks != 0) error << " and component locks";
        if (!request.control.drum_reference.empty()) error << " and drum conditioning";
        if (!request.control.melody_reference.empty()) error << " and melody conditioning";
        if (!request.control.chord_progression.empty()) error << " and harmony conditioning";
        if (needs_temporal_control(request)) error << " and temporal control";
        error << ". Install/register a capable provider instead of silently falling back to the wrong model.";
        throw std::runtime_error(error.str());
    }

    return *best;
}

RouteDecision ModelRouter::route(const GenerationRequest& request) const {
    GenerationRequest normalized = request;
    if (is_control_mode(request.mode)) {
        normalized = EtherControl{}.compile(request).request;
    }

    const RenderIntent resolved = resolve_intent(normalized);
    const auto& slot = select_provider(normalized, resolved);
    return RouteDecision{
        .provider_name = std::string{slot.backend->name()},
        .requested_intent = request.render_intent,
        .resolved_intent = resolved,
        .capabilities = slot.backend->capabilities()
    };
}

GenerationArtifact ModelRouter::generate(
    const GenerationRequest& request,
    const std::filesystem::path& output_directory) {

    if (request.prompt.empty()) throw std::invalid_argument("Generation prompt cannot be empty");
    if (request.duration_seconds <= 0.0 || request.duration_seconds > 600.0) {
        throw std::invalid_argument("Duration must be greater than 0 and no more than 600 seconds");
    }

    GenerationRequest normalized = request;
    if (is_control_mode(request.mode)) {
        normalized = EtherControl{}.compile(request).request;
    }

    const RenderIntent resolved = resolve_intent(normalized);
    const auto& slot = select_provider(normalized, resolved);

    // Composer remains provider-agnostic: choose the correct renderer first,
    // then compile the same structured blueprint for that provider.
    EtherComposer composer;
    const CompositionPlan plan = composer.compose(normalized);
    GenerationRequest compiled = composer.compile(normalized, plan);
    compiled.render_intent = resolved;

    auto artifact = slot.backend->generate(compiled, output_directory);
    persist_control_lineage(normalized, artifact, output_directory);
    return artifact;
}

std::unique_ptr<IModelBackend> make_default_backend() {
#ifdef _WIN32
    return make_managed_ace_step_backend();
#else
    return std::make_unique<MockWaveBackend>();
#endif
}

ModelRouter make_default_router() {
    ModelRouter router;
    router.add_provider(make_default_backend(), 100);
    return router;
}

} // namespace etherbeat
