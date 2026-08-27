#pragma once

#include "etherbeat/AudioAnalysis.hpp"
#include "etherbeat/EtherCritic.hpp"
#include "etherbeat/EtherDraft.hpp"
#include "etherbeat/GenerationTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace etherbeat {

class ModelRouter;

using AudioAnalyzer = std::function<AudioAnalysis(const std::filesystem::path&)>;

struct SearchOptions {
    DraftOptions draft{};
    CriticWeights critic{};
    bool promote_quality{true};
};

struct SearchCandidateState {
    std::size_t candidate_index{0};
    std::uint64_t seed{0};
    bool generated{false};
    bool analysis_ready{false};
    bool dna_persisted{false};
    std::filesystem::path audio_path;
    std::string analysis_error;
};

struct SearchReport {
    std::string search_id;
    std::filesystem::path manifest_path;
    DraftBatch draft_batch;
    CriticReport critic_report;
    std::vector<SearchCandidateState> candidates;

    // The Critic-selected draft remains explicit even when a later Quality pass
    // becomes the final winner exposed to the UI.
    std::optional<std::size_t> draft_winner_candidate_index;
    std::filesystem::path draft_winner_audio_path;
    std::uint64_t draft_winner_seed{0};

    bool quality_promoted{false};
    std::filesystem::path promotion_manifest_path;
    double quality_preservation_score{0.0};
    std::string promotion_error;

    std::optional<std::size_t> winner_candidate_index;
    std::filesystem::path winner_audio_path;
    std::uint64_t winner_seed{0};
    std::size_t analyzed_count{0};
    std::size_t dna_count{0};

    [[nodiscard]] bool has_winner() const noexcept;
};

class EtherSearch {
public:
    [[nodiscard]] SearchReport run(
        ModelRouter& router,
        const GenerationRequest& request,
        const std::filesystem::path& output_directory,
        const AudioAnalyzer& analyzer,
        SearchOptions options = {}) const;
};

} // namespace etherbeat
