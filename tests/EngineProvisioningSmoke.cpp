#include "etherbeat/EngineProvisioning.hpp"

#include <cassert>
#include <string>

int main() {
    using etherbeat::EngineProvisionState;

    assert(etherbeat::infer_engine_provision_state(false, false, false, false, false) == EngineProvisionState::RuntimeMissing);
    assert(etherbeat::infer_engine_provision_state(true, false, false, false, false) == EngineProvisionState::RuntimeReady);
    assert(etherbeat::infer_engine_provision_state(true, true, false, false, false) == EngineProvisionState::ModelMissing);
    assert(etherbeat::infer_engine_provision_state(true, true, true, false, false) == EngineProvisionState::ModelLoading);
    assert(etherbeat::infer_engine_provision_state(true, true, true, true, false) == EngineProvisionState::Warmup);
    assert(etherbeat::infer_engine_provision_state(true, true, true, true, true) == EngineProvisionState::Ready);

    etherbeat::EngineProvisionStatus status;
    status.state = EngineProvisionState::Ready;
    status.runtime_installed = true;
    status.api_online = true;
    status.model_files_present = true;
    status.model_loaded = true;
    status.warmup_passed = true;
    assert(status.ready());

    status.model_loaded = false;
    assert(!status.ready());

    assert(std::string(etherbeat::engine_provision_state_name(EngineProvisionState::ModelDownloading)) == "MODEL_DOWNLOADING");
    assert(std::string(etherbeat::engine_provision_state_name(EngineProvisionState::Warmup)) == "WARMUP_SELF_TEST");
    assert(std::string(etherbeat::engine_provision_state_name(EngineProvisionState::Failed)) == "FAILED");
    return 0;
}
