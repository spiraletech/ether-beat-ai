#pragma once

#include "etherbeat/EtherDraft.hpp"
#include "etherbeat/GenerationTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace etherbeat {

struct CriticWeights {
    double energy{0.22};
    double bass{0.24};
    double rhythm{0.24};
    double spectral_character{0.18};
    double texture{0.12};
};

struct CriticScore {
    std::size_t candidate_index{0};
    std::uint64_t seed{0};
    bool eligible{false};
    bool dna_available{false};

    double total{0.0};
    double energy_match{0.0};
    double bass_match{0.0};
    double rhythm_match{0.0};
    double spectral_match{0.0};
    double texture_match{0.0};

    std::filesystem::path audio_path;
    std::string provider;
    std::string note;
};

struct CriticReport {
    std::string batch_id;
    std::filesystem::path manifest_path;
    std::vector<CriticScore> ranked;
    std::optional<std::size_t> winner_candidate_index;
    std::filesystem::path winner_audio_path;
    std::uint64_t winner_seed{0};

    [[nodiscard]] bool has_winner() const noexcept;
};

class EtherCritic {
public:
    [[nodiscard]] CriticReport rank(
        const DraftBatch& batch,
        const GenerationRequest& source_request,
        const std::filesystem::path& output_directory,
        CriticWeights weights = {}) const;
};

} // namespace etherbeat
