#pragma once

#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/IModelBackend.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace etherbeat {

struct ProviderInfo {
    std::string name;
    ProviderCapabilities capabilities{0};
    int priority{0};
};

struct RouteDecision {
    std::string provider_name;
    RenderIntent requested_intent{RenderIntent::Auto};
    RenderIntent resolved_intent{RenderIntent::Quality};
    ProviderCapabilities capabilities{0};
};

class ModelRouter {
public:
    ModelRouter() = default;
    explicit ModelRouter(std::unique_ptr<IModelBackend> backend, int priority = 0);

    void add_provider(std::unique_ptr<IModelBackend> backend, int priority = 0);

    [[nodiscard]] std::size_t provider_count() const noexcept;
    [[nodiscard]] std::vector<ProviderInfo> providers() const;
    [[nodiscard]] RouteDecision route(const GenerationRequest& request) const;
    [[nodiscard]] const IModelBackend& backend() const;

    GenerationArtifact generate(
        const GenerationRequest& request,
        const std::filesystem::path& output_directory);

private:
    struct ProviderSlot {
        std::unique_ptr<IModelBackend> backend;
        int priority{0};
    };

    [[nodiscard]] const ProviderSlot& select_provider(
        const GenerationRequest& request,
        RenderIntent resolved_intent) const;

    std::vector<ProviderSlot> providers_;
};

// Compatibility seam for callers that explicitly want one backend.
std::unique_ptr<IModelBackend> make_default_backend();

// Production factory. This is where future Draft / Quality / Control / Vocal
// providers are registered without changing the EtherBeat UI.
ModelRouter make_default_router();

} // namespace etherbeat
