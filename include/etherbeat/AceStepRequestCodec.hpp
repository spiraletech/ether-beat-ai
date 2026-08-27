#pragma once

#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/ProviderTypes.hpp"

#include <string>

namespace etherbeat {

struct AceStepRequestPayload {
    std::string task_type;
    std::string json_body;
};

[[nodiscard]] ProviderCapabilities ace_step_provider_capabilities() noexcept;
[[nodiscard]] AceStepRequestPayload build_ace_step_request_payload(const GenerationRequest& request);

} // namespace etherbeat
