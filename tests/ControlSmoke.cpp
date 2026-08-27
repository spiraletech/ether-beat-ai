#include "etherbeat/EtherControl.hpp"
#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/IModelBackend.hpp"
#include "etherbeat/ModelRouter.hpp"
#include "etherbeat/ProviderTypes.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

class ControlBackend final : public etherbeat::IModelBackend {
public:
    ControlBackend(std::string name, etherbeat::ProviderCapabilities capabilities)
        : name_(std::move(name)), capabilities_(capabilities) {}

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] etherbeat::ProviderCapabilities capabilities() const noexcept override { return capabilities_; }

    etherbeat::GenerationArtifact generate(
        const etherbeat::GenerationRequest&,
        const std::filesystem::path&) override {
        throw std::runtime_error("ControlSmoke backend is routing-only");
    }

private:
    std::string name_;
    etherbeat::ProviderCapabilities capabilities_{};
};

etherbeat::ProviderCapabilities basic_control_caps() {
    return etherbeat::capability(etherbeat::ProviderCapability::Variation)
        | etherbeat::ProviderCapability::Extend
        | etherbeat::ProviderCapability::AudioToAudio
        | etherbeat::ProviderCapability::ReferenceAudio
        | etherbeat::ProviderCapability::ControlRole;
}

etherbeat::ProviderCapabilities precision_control_caps() {
    return basic_control_caps()
        | etherbeat::ProviderCapability::ReplaceSection
        | etherbeat::ProviderCapability::DrumConditioning
        | etherbeat::ProviderCapability::MelodyConditioning
        | etherbeat::ProviderCapability::HarmonyConditioning
        | etherbeat::ProviderCapability::ComponentLocks
        | etherbeat::ProviderCapability::TemporalControl;
}

} // namespace

int main() {
    try {
        etherbeat::GenerationRequest request;
        request.prompt = "replace the hook texture but preserve the pocket and melody";
        request.mode = etherbeat::GenerationMode::ReplaceSection;
        request.reference_audio = "source.wav";
        request.duration_seconds = 32.0;
        request.bpm = 68.0;
        request.key = "F# minor";
        request.mutation_amount = 0.28;
        request.control.reference_strength = 0.88;
        request.control.edit_start_seconds = 8.0;
        request.control.edit_end_seconds = 16.0;
        request.control.locks =
            etherbeat::ControlComponent::Drums |
            etherbeat::ControlComponent::Bass |
            etherbeat::ControlComponent::Melody |
            etherbeat::ControlComponent::Harmony |
            etherbeat::ControlComponent::Arrangement;
        request.control.drum_reference = "drums.wav";
        request.control.melody_reference = "melody.wav";
        request.control.chord_progression = "F#m9 - Dmaj7 - Aadd9 - E6";

        const auto plan = etherbeat::EtherControl{}.compile(request);
        if (plan.request.render_intent != etherbeat::RenderIntent::Control ||
            plan.locked_components.size() != 5 ||
            plan.summary.find("replace-section") == std::string::npos ||
            plan.summary.find("window 8.00-16.00 sec") == std::string::npos ||
            plan.request.prompt.find("Control blueprint:") == std::string::npos ||
            plan.request.prompt.find("Locked components are preservation constraints") == std::string::npos ||
            plan.request.prompt.find("F#m9 - Dmaj7 - Aadd9 - E6") == std::string::npos) {
            std::cerr << "EtherControl did not compile producer constraints correctly\n";
            return 1;
        }

        // Compilation must be idempotent when an already-compiled request is routed again.
        const auto second = etherbeat::EtherControl{}.compile(plan.request);
        const auto first_marker = second.request.prompt.find("Control blueprint:");
        if (first_marker == std::string::npos ||
            second.request.prompt.find("Control blueprint:", first_marker + 1) != std::string::npos) {
            std::cerr << "EtherControl duplicated its control blueprint\n";
            return 1;
        }

        etherbeat::ModelRouter router;
        router.add_provider(std::make_unique<ControlBackend>("basic-control", basic_control_caps()), 100);
        router.add_provider(std::make_unique<ControlBackend>("precision-control", precision_control_caps()), 50);

        const auto decision = router.route(request);
        if (decision.provider_name != "precision-control" ||
            decision.resolved_intent != etherbeat::RenderIntent::Control) {
            std::cerr << "Precision control request routed to an incapable provider\n";
            return 1;
        }

        // A basic variation without locks/temporal conditioning may use the simpler provider.
        etherbeat::GenerationRequest variation;
        variation.prompt = "make it darker but keep the same song identity";
        variation.mode = etherbeat::GenerationMode::Variation;
        variation.reference_audio = "source.wav";
        variation.mutation_amount = 0.20;
        const auto basic = router.route(variation);
        if (basic.provider_name != "basic-control") {
            std::cerr << "Basic variation did not use the highest-priority capable control provider\n";
            return 1;
        }

        // Missing precision capabilities must fail instead of silently dropping locks.
        etherbeat::ModelRouter weak_router;
        weak_router.add_provider(std::make_unique<ControlBackend>("basic-only", basic_control_caps()), 100);
        bool rejected = false;
        try {
            static_cast<void>(weak_router.route(request));
        } catch (const std::runtime_error& error) {
            rejected = std::string{error.what()}.find("component locks") != std::string::npos;
        }
        if (!rejected) {
            std::cerr << "Router silently degraded a precision control request\n";
            return 1;
        }

        // Replace Section must always have a valid temporal window.
        auto invalid = request;
        invalid.control.edit_end_seconds = -1.0;
        bool invalid_window_rejected = false;
        try {
            static_cast<void>(etherbeat::EtherControl{}.compile(invalid));
        } catch (const std::invalid_argument&) {
            invalid_window_rejected = true;
        }
        if (!invalid_window_rejected) {
            std::cerr << "EtherControl accepted an incomplete edit window\n";
            return 1;
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
