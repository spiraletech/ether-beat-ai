#include "etherbeat/EtherComposer.hpp"
#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/MockWaveBackend.hpp"
#include "etherbeat/ModelRouter.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

int main() {
    const std::filesystem::path output = "etherbeat-test-output";
    std::filesystem::remove_all(output);

    try {
        etherbeat::GenerationRequest composerRequest;
        composerRequest.prompt = "haunted cloud-rap instrumental, enormous negative space, submerged bass, beautifully degraded";
        composerRequest.duration_seconds = 20.0;
        composerRequest.bpm = 68.0;
        composerRequest.key = "F# minor";

        etherbeat::EtherComposer composer;
        const auto plan = composer.compose(composerRequest);
        if (plan.drum_density >= 0.48 || plan.vocal_space <= 0.70 || plan.bass_weight <= 0.65 || plan.texture_grit <= 0.65) {
            std::cerr << "EtherComposer did not translate production language into expected controls\n";
            return 1;
        }
        if (plan.sections.size() < 4 ||
            plan.renderer_prompt.find("Composition blueprint") == std::string::npos ||
            plan.renderer_prompt.find("Avoid festival-style EDM") == std::string::npos) {
            std::cerr << "EtherComposer blueprint is incomplete\n";
            return 1;
        }

        etherbeat::GenerationRequest request;
        request.prompt = "etherbeat smoke test";
        request.duration_seconds = 0.05;
        request.seed = 1444;
        request.bpm = 72.0;
        request.key = "F# minor";

        etherbeat::ModelRouter router{std::make_unique<etherbeat::MockWaveBackend>()};
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

        std::string text;
        {
            std::ifstream metadata{artifact.metadata_path};
            text.assign(
                std::istreambuf_iterator<char>{metadata},
                std::istreambuf_iterator<char>{});
        }

        if (text.find("etherbeat smoke test") == std::string::npos ||
            text.find("Composition blueprint") == std::string::npos ||
            text.find("\"seed\": 1444") == std::string::npos) {
            std::cerr << "Generation metadata is missing Composer lineage\n";
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
