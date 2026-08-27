#pragma once

#include <string>

namespace etherbeat {

enum class EngineProvisionState {
    RuntimeMissing,
    RuntimeDownloading,
    RuntimeExtracting,
    RuntimeReady,
    ApiStarting,
    ApiOnline,
    ModelMissing,
    ModelDownloading,
    ModelLoading,
    Warmup,
    Ready,
    Failed
};

struct EngineProvisionStatus {
    EngineProvisionState state{EngineProvisionState::RuntimeMissing};
    bool runtime_installed{false};
    bool api_online{false};
    bool model_files_present{false};
    bool model_loaded{false};
    bool warmup_passed{false};
    double progress{0.0};
    std::string detail;
    std::string error;

    [[nodiscard]] bool ready() const noexcept {
        return state == EngineProvisionState::Ready && runtime_installed && api_online &&
               model_loaded && warmup_passed;
    }
};

[[nodiscard]] const char* engine_provision_state_name(EngineProvisionState state) noexcept;

[[nodiscard]] EngineProvisionState infer_engine_provision_state(
    bool runtime_installed,
    bool api_online,
    bool model_files_present,
    bool model_loaded,
    bool warmup_passed) noexcept;

} // namespace etherbeat
