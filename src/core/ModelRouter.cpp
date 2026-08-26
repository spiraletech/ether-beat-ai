#include "etherbeat/ModelRouter.hpp"
#include "etherbeat/MockWaveBackend.hpp"

#include <stdexcept>
#include <utility>

namespace etherbeat {

ModelRouter::ModelRouter(std::unique_ptr<IModelBackend> backend)
    : backend_(std::move(backend)) {
    if (!backend_) {
        throw std::invalid_argument("ModelRouter requires a backend");
    }
}

const IModelBackend& ModelRouter::backend() const noexcept {
    return *backend_;
}

GenerationArtifact ModelRouter::generate(
    const GenerationRequest& request,
    const std::filesystem::path& output_directory) {
    if (request.prompt.empty()) {
        throw std::invalid_argument("Generation prompt cannot be empty");
    }
    if (request.duration_seconds <= 0.0 || request.duration_seconds > 600.0) {
        throw std::invalid_argument("Duration must be greater than 0 and no more than 600 seconds");
    }

    return backend_->generate(request, output_directory);
}

std::unique_ptr<IModelBackend> make_default_backend() {
    return std::make_unique<MockWaveBackend>();
}

} // namespace etherbeat
