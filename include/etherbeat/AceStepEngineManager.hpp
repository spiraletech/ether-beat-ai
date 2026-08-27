#pragma once

#include <filesystem>
#include <string_view>

namespace etherbeat {

// Ensures EtherBeat's private ACE-Step engine is installed and healthy.
// On first use this may download the official Windows portable runtime.
void ensure_managed_ace_step_engine();

// True when a compatible local engine already exists on disk.
[[nodiscard]] bool managed_ace_step_runtime_installed() noexcept;

// True when the localhost API is currently responding.
[[nodiscard]] bool managed_ace_step_engine_ready() noexcept;

// Location used for EtherBeat-owned model runtime files.
[[nodiscard]] std::filesystem::path managed_ace_step_runtime_root();

// Explicit shutdown is optional: the Windows Job Object also uses
// JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, so the child tree dies with EtherBeat.
void shutdown_managed_ace_step_engine() noexcept;

} // namespace etherbeat
