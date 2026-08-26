#pragma once

#include "etherbeat/GenerationTypes.hpp"

#include <filesystem>
#include <string_view>

namespace etherbeat {

class IModelBackend {
public:
    virtual ~IModelBackend() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    virtual GenerationArtifact generate(
        const GenerationRequest& request,
        const std::filesystem::path& output_directory) = 0;
};

} // namespace etherbeat
