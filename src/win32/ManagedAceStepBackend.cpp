#include "etherbeat/ManagedAceStepBackend.hpp"

#include "etherbeat/AceStepApiBackend.hpp"
#include "etherbeat/AceStepEngineManager.hpp"

#include <memory>
#include <string_view>

namespace etherbeat {
namespace {

class ManagedAceStepBackend final : public IModelBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "ace-step-1.5-managed-local";
    }

    GenerationArtifact generate(
        const GenerationRequest& request,
        const std::filesystem::path& output_directory) override {

        ensure_managed_ace_step_engine();
        AceStepApiBackend delegate;
        return delegate.generate(request, output_directory);
    }
};

} // namespace

std::unique_ptr<IModelBackend> make_managed_ace_step_backend() {
    return std::make_unique<ManagedAceStepBackend>();
}

} // namespace etherbeat
