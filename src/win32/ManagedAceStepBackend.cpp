#include "etherbeat/ManagedAceStepBackend.hpp"

#include "etherbeat/AceStepApiBackend.hpp"
#include "etherbeat/AceStepEngineManager.hpp"

#include <windows.h>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace etherbeat {
namespace {

void pump_windows_messages(bool& quitSeen) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            quitSeen = true;
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

class ManagedAceStepBackend final : public IModelBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "ace-step-1.5-managed-local";
    }

    GenerationArtifact generate(
        const GenerationRequest& request,
        const std::filesystem::path& output_directory) override {

        // The public backend contract remains synchronous, but the expensive local
        // runtime/model work is moved to a worker so the Win32 UI can continue
        // painting, moving, and responding instead of becoming Not Responding.
        auto task = std::async(std::launch::async, [request, output_directory] {
            ensure_managed_ace_step_engine();
            AceStepApiBackend delegate;
            return delegate.generate(request, output_directory);
        });

        bool quitSeen = false;
        while (task.wait_for(std::chrono::milliseconds(40)) != std::future_status::ready) {
            pump_windows_messages(quitSeen);
            if (quitSeen) {
                shutdown_managed_ace_step_engine();
            }
        }

        try {
            auto artifact = task.get();
            if (quitSeen) PostQuitMessage(0);
            return artifact;
        } catch (...) {
            if (quitSeen) PostQuitMessage(0);
            throw;
        }
    }
};

} // namespace

std::unique_ptr<IModelBackend> make_managed_ace_step_backend() {
    return std::make_unique<ManagedAceStepBackend>();
}

} // namespace etherbeat
