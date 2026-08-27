#pragma once

#include "etherbeat/IModelBackend.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace etherbeat {

class AceStepApiBackend final : public IModelBackend {
public:
    AceStepApiBackend(std::wstring host = L"127.0.0.1", std::uint16_t port = 8001);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] ProviderCapabilities capabilities() const noexcept override;

    GenerationArtifact generate(
        const GenerationRequest& request,
        const std::filesystem::path& output_directory) override;

    [[nodiscard]] bool server_ready() const noexcept;

private:
    std::wstring host_;
    std::uint16_t port_{};
};

[[nodiscard]] std::unique_ptr<IModelBackend> make_ace_step_backend();
[[nodiscard]] bool ace_step_server_ready() noexcept;

} // namespace etherbeat
