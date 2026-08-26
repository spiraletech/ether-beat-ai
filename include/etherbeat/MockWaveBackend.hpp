#pragma once

#include "etherbeat/IModelBackend.hpp"

namespace etherbeat {

class MockWaveBackend final : public IModelBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override;

    GenerationArtifact generate(
        const GenerationRequest& request,
        const std::filesystem::path& output_directory) override;
};

} // namespace etherbeat
