#pragma once

#include "etherbeat/EngineProvisioning.hpp"

#include <filesystem>

namespace etherbeat {

// Provision, launch, initialize and self-test EtherBeat's private ACE-Step engine.
// This function returns only after a real model-backed generation warmup succeeds.
void ensure_managed_ace_step_engine();

// True when a compatible local engine runtime exists on disk.
[[nodiscard]] bool managed_ace_step_runtime_installed() noexcept;

// True only after runtime + API + loaded DiT model + warmup generation are all proven.
[[nodiscard]] bool managed_ace_step_engine_ready() noexcept;

// Cached live provisioning state for UI diagnostics. This call does not perform network I/O.
[[nodiscard]] EngineProvisionStatus managed_ace_step_engine_status() noexcept;

// Location used for EtherBeat-owned model runtime files.
[[nodiscard]] std::filesystem::path managed_ace_step_runtime_root();

// Server log path used for actionable startup/model-loading failures.
[[nodiscard]] std::filesystem::path managed_ace_step_log_path();

// Explicit shutdown is optional: the Windows Job Object also uses
// JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, so the child tree dies with EtherBeat.
void shutdown_managed_ace_step_engine() noexcept;

} // namespace etherbeat
