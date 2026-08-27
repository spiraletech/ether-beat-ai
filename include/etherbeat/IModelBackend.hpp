#pragma once

#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/ProviderTypes.hpp"

#include <filesystem>
#include <string_view>

namespace etherbeat {

class IModelBackend {
public:
    virtual ~IModelBackend() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // Existing text-to-music renderers remain source-compatible while newer
    // providers can advertise more specialized capabilities explicitly.
    [[nodiscard]] virtual ProviderCapabilities capabilities() const noexcept {
        return capability(ProviderCapability::TextToInstrumental)
            | ProviderCapability::DraftRole
            | ProviderCapability::QualityRole;
    }

    virtual GenerationArtifact generate(
        const GenerationRequest& request,
        const std::filesystem::path& output_directory) = 0;
};

} // namespace etherbeat
