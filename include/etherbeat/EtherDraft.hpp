#pragma once

#include "etherbeat/GenerationTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace etherbeat {

class ModelRouter;

struct DraftOptions {
    std::size_t candidate_count{4};
    bool continue_after_failure{true};
};

struct DraftCandidate {
    std::size_t index{0};
    std::uint64_t seed{0};
    bool success{false};
    GenerationArtifact artifact;
    std::string error;
};

struct DraftBatch {
    std::string batch_id;
    std::uint64_t base_seed{0};
    std::filesystem::path manifest_path;
    std::vector<DraftCandidate> candidates;

    [[nodiscard]] std::size_t success_count() const noexcept;
    [[nodiscard]] bool has_success() const noexcept;
};

class EtherDraft {
public:
    [[nodiscard]] DraftBatch generate(
        ModelRouter& router,
        const GenerationRequest& request,
        const std::filesystem::path& output_directory,
        DraftOptions options = {}) const;
};

} // namespace etherbeat
