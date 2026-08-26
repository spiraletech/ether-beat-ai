#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/ModelRouter.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main() {
    const std::filesystem::path output = "etherbeat-test-output";
    std::filesystem::remove_all(output);

    try {
        etherbeat::GenerationRequest request;
        request.prompt = "etherbeat smoke test";
        request.duration_seconds = 0.05;
        request.seed = 1444;
        request.bpm = 72.0;
        request.key = "F# minor";

        etherbeat::ModelRouter router{etherbeat::make_default_backend()};
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

        std::ifstream metadata{artifact.metadata_path};
        const std::string text{
            std::istreambuf_iterator<char>{metadata},
            std::istreambuf_iterator<char>{}};

        if (text.find("etherbeat smoke test") == std::string::npos ||
            text.find("\"seed\": 1444") == std::string::npos) {
            std::cerr << "Generation metadata is incomplete\n";
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
