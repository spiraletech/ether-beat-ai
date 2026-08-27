#include "etherbeat/EtherComposer.hpp"
#include "etherbeat/EtherDNA.hpp"
#include "etherbeat/EtherDraft.hpp"
#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/IModelBackend.hpp"
#include "etherbeat/MockWaveBackend.hpp"
#include "etherbeat/ModelRouter.hpp"
#include "etherbeat/ProviderTypes.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

namespace {

class RouteBackend final : public etherbeat::IModelBackend {
public:
    RouteBackend(std::string name, etherbeat::ProviderCapabilities capabilities)
        : name_(std::move(name)), capabilities_(capabilities) {}

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] etherbeat::ProviderCapabilities capabilities() const noexcept override { return capabilities_; }

    etherbeat::GenerationArtifact generate(
        const etherbeat::GenerationRequest&,
        const std::filesystem::path&) override {
        throw std::runtime_error("RouteBackend is routing-only");
    }

private:
    std::string name_;
    etherbeat::ProviderCapabilities capabilities_{};
};

etherbeat::ProviderCapabilities caps(
    etherbeat::ProviderCapability mode,
    etherbeat::ProviderCapability role,
    bool reference = false) {

    auto value = etherbeat::capability(mode) | role;
    if (reference) value = value | etherbeat::ProviderCapability::ReferenceAudio;
    return value;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>{in},
        std::istreambuf_iterator<char>{});
}

} // namespace

int main() {
    const std::filesystem::path output = "etherbeat-test-output";
    std::filesystem::remove_all(output);
    std::filesystem::create_directories(output);

    try {
        // Provider routing must choose by musical job, not provider name.
        etherbeat::ModelRouter providerRouter;
        providerRouter.add_provider(std::make_unique<RouteBackend>(
            "draft-provider",
            caps(etherbeat::ProviderCapability::TextToInstrumental, etherbeat::ProviderCapability::DraftRole)), 30);
        providerRouter.add_provider(std::make_unique<RouteBackend>(
            "quality-provider",
            caps(etherbeat::ProviderCapability::TextToInstrumental, etherbeat::ProviderCapability::QualityRole)), 20);
        providerRouter.add_provider(std::make_unique<RouteBackend>(
            "control-provider",
            caps(etherbeat::ProviderCapability::Variation, etherbeat::ProviderCapability::ControlRole, true)), 10);

        etherbeat::GenerationRequest routedRequest;
        routedRequest.prompt = "routing test";
        routedRequest.duration_seconds = 20.0;

        const auto qualityRoute = providerRouter.route(routedRequest);
        if (qualityRoute.provider_name != "quality-provider" ||
            qualityRoute.resolved_intent != etherbeat::RenderIntent::Quality) {
            std::cerr << "Auto text generation did not route to the Quality provider\n";
            return 1;
        }

        routedRequest.render_intent = etherbeat::RenderIntent::Draft;
        const auto draftRoute = providerRouter.route(routedRequest);
        if (draftRoute.provider_name != "draft-provider" ||
            draftRoute.resolved_intent != etherbeat::RenderIntent::Draft) {
            std::cerr << "Explicit Draft generation did not route to the Draft provider\n";
            return 1;
        }

        routedRequest.render_intent = etherbeat::RenderIntent::Auto;
        routedRequest.mode = etherbeat::GenerationMode::Variation;
        routedRequest.reference_audio = output / "routing-reference.wav";
        const auto controlRoute = providerRouter.route(routedRequest);
        if (controlRoute.provider_name != "control-provider" ||
            controlRoute.resolved_intent != etherbeat::RenderIntent::Control) {
            std::cerr << "Variation did not route to a reference-capable Control provider\n";
            return 1;
        }

        bool vocalRejected = false;
        try {
            routedRequest.mode = etherbeat::GenerationMode::TextToInstrumental;
            routedRequest.reference_audio.clear();
            routedRequest.render_intent = etherbeat::RenderIntent::Vocal;
            static_cast<void>(providerRouter.route(routedRequest));
        } catch (const std::runtime_error&) {
            vocalRejected = true;
        }
        if (!vocalRejected) {
            std::cerr << "Unsupported Vocal routing silently fell back to the wrong provider\n";
            return 1;
        }

        // EtherDraft must create a deterministic, lineage-preserving candidate set.
        etherbeat::ModelRouter draftRouter{std::make_unique<etherbeat::MockWaveBackend>()};
        etherbeat::GenerationRequest draftRequest;
        draftRequest.prompt = "haunted draft batch";
        draftRequest.duration_seconds = 0.05;
        draftRequest.seed = 1444;
        draftRequest.bpm = 68.0;
        draftRequest.key = "F# minor";

        etherbeat::EtherDraft draftEngine;
        const auto batch = draftEngine.generate(
            draftRouter,
            draftRequest,
            output,
            etherbeat::DraftOptions{.candidate_count = 4, .continue_after_failure = true});

        if (batch.base_seed != 1444u || batch.candidates.size() != 4 ||
            batch.success_count() != 4 || !batch.has_success() ||
            !std::filesystem::exists(batch.manifest_path)) {
            std::cerr << "EtherDraft did not produce the requested candidate batch\n";
            return 1;
        }

        std::set<std::uint64_t> draftSeeds;
        for (const auto& candidate : batch.candidates) {
            if (!candidate.success || candidate.seed == 0 ||
                !std::filesystem::exists(candidate.artifact.audio_path) ||
                !std::filesystem::exists(candidate.artifact.metadata_path)) {
                std::cerr << "EtherDraft candidate artifact is incomplete\n";
                return 1;
            }
            draftSeeds.insert(candidate.seed);
            const auto metadata = read_text(candidate.artifact.metadata_path);
            if (metadata.find("\"render_intent\": \"draft\"") == std::string::npos) {
                std::cerr << "EtherDraft candidate lost Draft provider lineage\n";
                return 1;
            }
        }
        if (draftSeeds.size() != 4 || batch.candidates.front().seed != 1444u) {
            std::cerr << "EtherDraft candidate seeds are not stable and distinct\n";
            return 1;
        }

        const auto draftManifest = read_text(batch.manifest_path);
        if (draftManifest.find("\"schema\": \"etherbeat.draft.v1\"") == std::string::npos ||
            draftManifest.find("\"success_count\": 4") == std::string::npos ||
            draftManifest.find("haunted draft batch") == std::string::npos) {
            std::cerr << "EtherDraft manifest is missing batch lineage\n";
            return 1;
        }

        const std::filesystem::path referenceAudio = output / "reference.wav";

        etherbeat::AudioAnalysis analysis;
        analysis.ready = true;
        analysis.sample_rate = 48000;
        analysis.channels = 2;
        analysis.analyzed_windows = 1444;
        analysis.duration_seconds = 20.0;
        analysis.energy = 0.42f;
        analysis.bass = 0.82f;
        analysis.mid = 0.48f;
        analysis.treble = 0.19f;
        analysis.beat_peak = 0.31f;
        for (std::size_t i = 0; i < analysis.spectrum.size(); ++i) {
            analysis.spectrum[i] = static_cast<float>(i + 1) / static_cast<float>(analysis.spectrum.size());
        }

        const auto dna = etherbeat::make_ether_dna(referenceAudio, analysis);
        const auto dnaPath = etherbeat::ether_dna_sidecar_path(referenceAudio);
        if (!etherbeat::save_ether_dna(dna, dnaPath)) {
            std::cerr << "EtherDNA sidecar could not be written\n";
            return 1;
        }

        const auto loaded = etherbeat::load_ether_dna_for_audio(referenceAudio);
        if (!loaded || loaded->schema != "etherbeat.dna.v1" || loaded->sample_rate != 48000 ||
            std::abs(loaded->bass - 0.82f) > 0.001f || loaded->low_end_weight <= 0.65f ||
            loaded->conditioning_summary().find("reference DNA") == std::string::npos) {
            std::cerr << "EtherDNA round-trip lost measurable audio identity\n";
            return 1;
        }

        etherbeat::GenerationRequest composerRequest;
        composerRequest.prompt = "haunted cloud-rap instrumental, enormous negative space, submerged bass, beautifully degraded";
        composerRequest.duration_seconds = 20.0;
        composerRequest.bpm = 68.0;
        composerRequest.key = "F# minor";
        composerRequest.reference_audio = referenceAudio;

        etherbeat::EtherComposer composer;
        const auto plan = composer.compose(composerRequest);
        if (plan.drum_density >= 0.48 || plan.vocal_space <= 0.70 || plan.bass_weight <= 0.65 || plan.texture_grit <= 0.65) {
            std::cerr << "EtherComposer did not translate production language into expected controls\n";
            return 1;
        }
        if (plan.sections.size() < 4 ||
            plan.renderer_prompt.find("Composition blueprint") == std::string::npos ||
            plan.renderer_prompt.find("Avoid festival-style EDM") == std::string::npos ||
            plan.renderer_prompt.find("Measured reference DNA") == std::string::npos) {
            std::cerr << "EtherComposer blueprint is missing EtherDNA conditioning\n";
            return 1;
        }

        etherbeat::GenerationRequest request;
        request.prompt = "etherbeat smoke test";
        request.duration_seconds = 0.05;
        request.seed = 1444;
        request.bpm = 72.0;
        request.key = "F# minor";

        etherbeat::ModelRouter router{std::make_unique<etherbeat::MockWaveBackend>()};
        const auto route = router.route(request);
        if (route.provider_name != "mock-wave-48k" || route.resolved_intent != etherbeat::RenderIntent::Quality) {
            std::cerr << "Single-provider compatibility routing failed\n";
            return 1;
        }

        const auto artifact = router.generate(request, output);

        if (!std::filesystem::exists(artifact.audio_path)) {
            std::cerr << "WAV artifact was not created\n";
            return 1;
        }
        if (!std::filesystem::exists(artifact.metadata_path)) {
            std::cerr << "Metadata artifact was not created\n";
            return 1;
        }
        if (std::filesystem::file_size(artifact.audio_path) <= 44u) {
            std::cerr << "WAV artifact does not contain PCM data\n";
            return 1;
        }
        if (artifact.resolved_seed != 1444u) {
            std::cerr << "Seed lineage was not preserved\n";
            return 1;
        }

        const auto text = read_text(artifact.metadata_path);
        if (text.find("etherbeat smoke test") == std::string::npos ||
            text.find("Composition blueprint") == std::string::npos ||
            text.find("\"render_intent\": \"quality\"") == std::string::npos ||
            text.find("\"seed\": 1444") == std::string::npos) {
            std::cerr << "Generation metadata is missing Composer/provider lineage\n";
            return 1;
        }

        std::filesystem::remove_all(output);
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(output);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
