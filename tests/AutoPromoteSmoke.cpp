#include "etherbeat/EtherSearch.hpp"
#include "etherbeat/MockWaveBackend.hpp"
#include "etherbeat/ModelRouter.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

etherbeat::AudioAnalysis analysis(
    float energy,
    float bass,
    float treble,
    float beat,
    float base,
    float step) {

    etherbeat::AudioAnalysis value;
    value.ready = true;
    value.sample_rate = 48000;
    value.channels = 2;
    value.analyzed_windows = 1024;
    value.duration_seconds = 20.0;
    value.energy = energy;
    value.bass = bass;
    value.mid = 0.45f;
    value.treble = treble;
    value.beat_peak = beat;
    for (std::size_t i = 0; i < value.spectrum.size(); ++i) {
        const float sample = base + step * static_cast<float>(i);
        value.spectrum[i] = sample < 0.0f ? 0.0f : (sample > 1.0f ? 1.0f : sample);
    }
    return value;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>{in},
        std::istreambuf_iterator<char>{});
}

} // namespace

int main() {
    const std::filesystem::path output = "etherbeat-auto-promote-test-output";
    std::filesystem::remove_all(output);
    std::filesystem::create_directories(output);

    try {
        etherbeat::ModelRouter router{std::make_unique<etherbeat::MockWaveBackend>()};

        etherbeat::GenerationRequest request;
        request.prompt = "haunted cloud-rap, negative space, submerged bass, degraded";
        request.duration_seconds = 0.05;
        request.seed = 2222;
        request.bpm = 68.0;
        request.key = "F# minor";

        const std::array<etherbeat::AudioAnalysis, 5> staged = {
            analysis(0.88f, 0.14f, 0.82f, 0.86f, 0.04f, 0.025f),
            analysis(0.58f, 0.42f, 0.50f, 0.61f, 0.18f, 0.008f),
            analysis(0.38f, 0.74f, 0.20f, 0.31f, 0.44f, -0.005f),
            analysis(0.72f, 0.22f, 0.65f, 0.75f, 0.08f, 0.018f),
            analysis(0.39f, 0.73f, 0.21f, 0.32f, 0.43f, -0.005f)
        };

        std::size_t calls = 0;
        etherbeat::AudioAnalyzer analyzer = [&](const std::filesystem::path&) {
            if (calls >= staged.size()) {
                etherbeat::AudioAnalysis failed;
                failed.error = "unexpected analyzer call";
                return failed;
            }
            return staged[calls++];
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

        if (!report.has_winner() || !report.draft_winner_candidate_index ||
            !report.winner_candidate_index ||
            !report.quality_promoted ||
            report.promotion_manifest_path.empty() ||
            !std::filesystem::exists(report.promotion_manifest_path) ||
            report.draft_winner_audio_path.empty() ||
            report.winner_audio_path.empty() ||
            report.draft_winner_audio_path == report.winner_audio_path ||
            report.winner_seed != report.draft_winner_seed ||
            report.quality_preservation_score < 0.95 ||
            calls != 5) {
            std::cerr << "EtherSearch default path did not promote the Critic winner through Quality\n";
            return 1;
        }

        if (report.winner_audio_path.parent_path().parent_path().filename() != "promoted") {
            std::cerr << "Promoted Quality artifact was not isolated from draft artifacts\n";
            return 1;
        }

        const auto manifest = read_text(report.manifest_path);
        if (manifest.find("\"quality_promoted\": true") == std::string::npos ||
            manifest.find("\"draft_winner_audio_path\"") == std::string::npos ||
            manifest.find("\"promotion_manifest\"") == std::string::npos) {
            std::cerr << "EtherSearch manifest did not preserve draft-to-quality promotion lineage\n";
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
