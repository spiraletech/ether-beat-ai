#include "etherbeat/EngineProvisioning.hpp"

namespace etherbeat {

const char* engine_provision_state_name(EngineProvisionState state) noexcept {
    switch (state) {
    case EngineProvisionState::RuntimeMissing: return "RUNTIME_MISSING";
    case EngineProvisionState::RuntimeDownloading: return "RUNTIME_DOWNLOADING";
    case EngineProvisionState::RuntimeExtracting: return "RUNTIME_EXTRACTING";
    case EngineProvisionState::RuntimeReady: return "RUNTIME_READY";
    case EngineProvisionState::ApiStarting: return "API_STARTING";
    case EngineProvisionState::ApiOnline: return "API_ONLINE";
    case EngineProvisionState::ModelMissing: return "MODEL_MISSING";
    case EngineProvisionState::ModelDownloading: return "MODEL_DOWNLOADING";
    case EngineProvisionState::ModelLoading: return "MODEL_LOADING";
    case EngineProvisionState::Warmup: return "WARMUP_SELF_TEST";
    case EngineProvisionState::Ready: return "READY";
    case EngineProvisionState::Failed: return "FAILED";
    }
    return "UNKNOWN";
}

EngineProvisionState infer_engine_provision_state(
    bool runtime_installed,
    bool api_online,
    bool model_files_present,
    bool model_loaded,
    bool warmup_passed) noexcept {

    if (!runtime_installed) return EngineProvisionState::RuntimeMissing;
    if (!api_online) return EngineProvisionState::RuntimeReady;
    if (!model_files_present && !model_loaded) return EngineProvisionState::ModelMissing;
    if (!model_loaded) return EngineProvisionState::ModelLoading;
    if (!warmup_passed) return EngineProvisionState::Warmup;
    return EngineProvisionState::Ready;
}

} // namespace etherbeat
