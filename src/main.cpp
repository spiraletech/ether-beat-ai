#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/ModelRouter.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        std::string prompt = "private pleiadian instrumental prototype";
        double duration = 10.0;

        if (argc >= 2) {
            prompt = argv[1];
        }
        if (argc >= 3) {
            duration = std::stod(argv[2]);
        }

        etherbeat::GenerationRequest request;
        request.prompt = prompt;
        request.duration_seconds = duration;

        etherbeat::ModelRouter router{etherbeat::make_default_backend()};
        const auto artifact = router.generate(request, "generated");

        std::cout
            << "ETHERBEAT " << ETHERBEAT_VERSION << '\n'
            << "backend: " << artifact.backend_name << '\n'
            << "seed: " << artifact.resolved_seed << '\n'
            << "audio: " << std::filesystem::absolute(artifact.audio_path).string() << '\n'
            << "metadata: " << std::filesystem::absolute(artifact.metadata_path).string() << '\n';

        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "ETHERBEAT ERROR: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
