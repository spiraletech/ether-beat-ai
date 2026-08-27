#pragma once

#include "etherbeat/GenerationTypes.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace etherbeat {

struct ControlPlan {
    GenerationRequest request;
    std::vector<std::string> locked_components;
    std::string summary;
};

class EtherControl {
public:
    [[nodiscard]] ControlPlan compile(const GenerationRequest& request) const;
};

} // namespace etherbeat
