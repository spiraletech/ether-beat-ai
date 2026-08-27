#include "etherbeat/ModelRouter.hpp"
#include "etherbeat/MockWaveBackend.hpp"
#include "etherbeat/EtherComposer.hpp"
#ifdef _WIN32
#include "etherbeat/ManagedAceStepBackend.hpp"
#endif

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

RenderIntent resolve_intent(const GenerationRequest& request) {
    if (request.render_intent != RenderIntent::Auto) return request.render_intent;

    switch (request.mode) {
    case GenerationMode::TextToInstrumental:
        return RenderIntent::Quality;
    case GenerationMode::Variation:
    case GenerationMode::Extend:
    case GenerationMode::AudioToAudio:
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
    }
    return "unknown";
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
        error << ". Install/register a capable provider instead of silently falling back to the wrong model.";
        throw std::runtime_error(error.str());
    }

    return *best;
}

RouteDecision ModelRouter::route(const GenerationRequest& request) const {
    const RenderIntent resolved = resolve_intent(request);
    const auto& slot = select_provider(request, resolved);
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

    if (request.mode != GenerationMode::TextToInstrumental && request.reference_audio.empty()) {
        throw std::invalid_argument("Variation, Extend and Audio-to-Audio jobs require reference audio");
    }

    const RenderIntent resolved = resolve_intent(request);
    const auto& slot = select_provider(request, resolved);

    // Composer remains provider-agnostic: choose the correct renderer first,
    // then compile the same structured blueprint for that provider.
    EtherComposer composer;
    const CompositionPlan plan = composer.compose(request);
    GenerationRequest compiled = composer.compile(request, plan);
    compiled.render_intent = resolved;

    return slot.backend->generate(compiled, output_directory);
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
