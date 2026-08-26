#pragma once

#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/IModelBackend.hpp"

#include <filesystem>
#include <memory>

namespace etherbeat {

class ModelRouter {
public:
    explicit ModelRouter(std::unique_ptr<IModelBackend> backend);

    [[nodiscard]] const IModelBackend& backend() const noexcept;

    GenerationArtifact generate(
        const GenerationRequest& request,
        const std::filesystem::path& output_directory);

private:
    std::unique_ptr<IModelBackend> backend_;
};

std::unique_ptr<IModelBackend> make_default_backend();

} // namespace etherbeat
