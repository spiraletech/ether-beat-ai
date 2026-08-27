#include "etherbeat/EtherDNA.hpp"
#include "etherbeat/EtherSearch.hpp"
#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/MockWaveBackend.hpp"
#include "etherbeat/ModelRouter.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>{in},
        std::istreambuf_iterator<char>{});
}

etherbeat::AudioAnalysis make_analysis(
    float energy,
    float bass,
    float treble,
    float beat,
    float spectrum_base,
    float spectrum_step) {

    etherbeat::AudioAnalysis analysis;
    analysis.ready = true;
    analysis.sample_rate = 48000;
    analysis.channels = 2;
    analysis.analyzed_windows = 2048;
    analysis.duration_seconds = 20.0;
    analysis.energy = energy;
    analysis.bass = bass;
    analysis.mid = 0.45f;
    analysis.treble = treble;
    analysis.beat_peak = beat;
    for (std::size_t i = 0; i < analysis.spectrum.size(); ++i) {
        const float value = spectrum_base + spectrum_step * static_cast<float>(i);
        analysis.spectrum[i] = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }
    return analysis;
}

} // namespace

int main() {
    const std::filesystem::path output = "etherbeat-search-test-output";
    std::filesystem::remove_all(output);
    std::filesystem::create_directories(output);

    try {
        etherbeat::ModelRouter router{std::make_unique<etherbeat::MockWaveBackend>()};

        etherbeat::GenerationRequest request;
        request.prompt = "haunted draft batch";
        request.duration_seconds = 0.05;
        request.seed = 1444;
        request.bpm = 68.0;
        request.key = "F# minor";

        const std::array<etherbeat::AudioAnalysis, 4> staged = {
            make_analysis(0.90f, 0.10f, 0.90f, 0.90f, 0.05f, 0.025f),
            make_analysis(0.58f, 0.38f, 0.52f, 0.62f, 0.18f, 0.008f),
            make_analysis(0.38f, 0.58f, 0.24f, 0.53f, 0.42f, -0.004f),
            etherbeat::AudioAnalysis{}
        };

        std::size_t analyzer_call = 0;
        etherbeat::AudioAnalyzer analyzer = [&](const std::filesystem::path&) {
            if (analyzer_call >= staged.size()) {
                etherbeat::AudioAnalysis failure;
                failure.error = "unexpected analyzer call";
                return failure;
            }
            auto result = staged[analyzer_call++];
            if (!result.ready && result.error.empty()) {
                result.error = "synthetic analyzer rejection";
            }
            return result;
        };

        etherbeat::EtherSearch search;
        const auto report = search.run(
            router,
            request,
            output,
            analyzer,
            etherbeat::SearchOptions{
                .draft = etherbeat::DraftOptions{.candidate_count = 4, .continue_after_failure = true},
                .critic = etherbeat::CriticWeights{}
            });

        if (report.draft_batch.candidates.size() != 4 ||
            report.draft_batch.success_count() != 4 ||
            report.candidates.size() != 4 ||
            report.analyzed_count != 3 ||
            report.dna_count != 3 ||
            !report.has_winner() ||
            !report.winner_candidate_index ||
            *report.winner_candidate_index != 2u ||
            report.winner_seed != report.draft_batch.candidates[2].seed ||
            report.winner_audio_path != report.draft_batch.candidates[2].artifact.audio_path) {
            std::cerr << "EtherSearch did not produce the expected analyzed/ranked search result\n";
            return 1;
        }

        for (std::size_t i = 0; i < 3; ++i) {
            const auto& candidate = report.candidates[i];
            if (!candidate.generated || !candidate.analysis_ready || !candidate.dna_persisted ||
                !std::filesystem::exists(etherbeat::ether_dna_sidecar_path(candidate.audio_path))) {
                std::cerr << "EtherSearch did not persist measured candidate DNA\n";
                return 1;
            }
        }

        if (!report.candidates[3].generated || report.candidates[3].analysis_ready ||
            report.candidates[3].dna_persisted ||
            report.candidates[3].analysis_error.find("synthetic analyzer rejection") == std::string::npos) {
            std::cerr << "EtherSearch did not preserve candidate-level analyzer failure state\n";
            return 1;
        }

        if (!std::filesystem::exists(report.draft_batch.manifest_path) ||
            !std::filesystem::exists(report.critic_report.manifest_path) ||
            !std::filesystem::exists(report.manifest_path)) {
            std::cerr << "EtherSearch lineage manifests are incomplete\n";
            return 1;
        }

        const auto manifest = read_text(report.manifest_path);
        if (manifest.find("\"schema\": \"etherbeat.search.v1\"") == std::string::npos ||
            manifest.find("\"candidate_count\": 4") == std::string::npos ||
            manifest.find("\"analyzed_count\": 3") == std::string::npos ||
            manifest.find("\"dna_count\": 3") == std::string::npos ||
            manifest.find("\"winner_candidate_index\": 2") == std::string::npos ||
            manifest.find("synthetic analyzer rejection") == std::string::npos) {
            std::cerr << "EtherSearch manifest is missing orchestration lineage\n";
            return 1;
        }

        if (report.critic_report.ranked.empty() ||
            report.critic_report.ranked.front().candidate_index != 2u ||
            report.critic_report.ranked.back().candidate_index != 3u ||
            report.critic_report.ranked.back().dna_available) {
            std::cerr << "EtherSearch did not hand measured candidates to EtherCritic correctly\n";
            return 1;
        }

        std::filesystem::remove_all(output);
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(output);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
