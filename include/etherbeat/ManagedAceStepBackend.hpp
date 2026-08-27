#pragma once

#include "etherbeat/IModelBackend.hpp"

#include <memory>

namespace etherbeat {

[[nodiscard]] std::unique_ptr<IModelBackend> make_managed_ace_step_backend();

} // namespace etherbeat
