#pragma once

#include "etherbeat/AudioAnalysis.hpp"
#include "etherbeat/EtherSearch.hpp"
#include "etherbeat/GenerationTypes.hpp"

#include <filesystem>
#include <string>

namespace etherbeat {

class ModelRouter;

struct PromotionReport {
    std::string promotion_id;
    std::filesystem::path manifest_path;

    std::size_t source_candidate_index{0};
    std::uint64_t source_seed{0};
    std::filesystem::path source_audio_path;

    bool source_dna_available{false};
    bool quality_generated{false};
    bool quality_analysis_ready{false};
    bool quality_dna_persisted{false};

    GenerationArtifact quality_artifact;
    AudioAnalysis quality_analysis{};
    double dna_preservation_score{0.0};
    std::string error;

    [[nodiscard]] bool succeeded() const noexcept;
};

class EtherPromote {
public:
    [[nodiscard]] PromotionReport run(
        ModelRouter& router,
        const SearchReport& search,
        const GenerationRequest& source_request,
        const std::filesystem::path& output_directory,
        const AudioAnalyzer& analyzer) const;
};

} // namespace etherbeat
