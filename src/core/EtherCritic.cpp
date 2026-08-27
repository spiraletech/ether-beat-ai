#include "etherbeat/EtherCritic.hpp"

#include "etherbeat/EtherComposer.hpp"
#include "etherbeat/EtherDNA.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace etherbeat {
namespace {

double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

double match01(double measured, double target) {
    return clamp01(1.0 - std::abs(clamp01(measured) - clamp01(target)));
}

bool has_tag(const CompositionPlan& plan, const char* value) {
    return std::find(plan.tags.begin(), plan.tags.end(), value) != plan.tags.end();
}

double target_brightness(const CompositionPlan& plan) {
    double target = 0.50;
    if (has_tag(plan, "haunted")) target -= 0.20;
    if (has_tag(plan, "degraded")) target -= 0.12;
    if (has_tag(plan, "polished")) target += 0.20;
    if (has_tag(plan, "floating")) target += 0.04;
    return clamp01(target);
}

double texture_proxy(const EtherDNA& dna) {
    // EtherDNA V0.1 does not yet contain learned texture embeddings or
    // spectral-flatness. This intentionally transparent proxy favors darker,
    // lower-centered spectra for degraded/gritty targets until those features
    // arrive in a later DNA revision.
    return clamp01(
        static_cast<double>(dna.darkness) * 0.65 +
        (1.0 - static_cast<double>(dna.spectral_center)) * 0.35);
}

std::string path_utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char c : value) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c >= 0x20) out << static_cast<char>(c);
            break;
        }
    }
    return out.str();
}

CriticWeights normalize_weights(CriticWeights weights) {
    weights.energy = std::max(0.0, weights.energy);
    weights.bass = std::max(0.0, weights.bass);
    weights.rhythm = std::max(0.0, weights.rhythm);
    weights.spectral_character = std::max(0.0, weights.spectral_character);
    weights.texture = std::max(0.0, weights.texture);

    const double sum = weights.energy + weights.bass + weights.rhythm +
        weights.spectral_character + weights.texture;
    if (sum <= 1e-9) return CriticWeights{};

    weights.energy /= sum;
    weights.bass /= sum;
    weights.rhythm /= sum;
    weights.spectral_character /= sum;
    weights.texture /= sum;
    return weights;
}

void write_manifest(
    CriticReport& report,
    const CompositionPlan& target,
    const CriticWeights& weights) {

    std::ofstream out(report.manifest_path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("EtherCritic could not create its ranking manifest");

    out << std::fixed << std::setprecision(6)
        << "{\n"
        << "  \"schema\": \"etherbeat.critic.v1\",\n"
        << "  \"batch_id\": \"" << json_escape(report.batch_id) << "\",\n"
        << "  \"target\": {\n"
        << "    \"energy\": " << target.energy << ",\n"
        << "    \"bass_weight\": " << target.bass_weight << ",\n"
        << "    \"rhythmic_activity\": " << ((target.drum_density + target.transient_density) * 0.5) << ",\n"
        << "    \"brightness\": " << target_brightness(target) << ",\n"
        << "    \"texture_grit\": " << target.texture_grit << "\n"
        << "  },\n"
        << "  \"weights\": {\n"
        << "    \"energy\": " << weights.energy << ",\n"
        << "    \"bass\": " << weights.bass << ",\n"
        << "    \"rhythm\": " << weights.rhythm << ",\n"
        << "    \"spectral_character\": " << weights.spectral_character << ",\n"
        << "    \"texture\": " << weights.texture << "\n"
        << "  },\n"
        << "  \"winner_candidate_index\": ";

    if (report.winner_candidate_index) out << *report.winner_candidate_index;
    else out << "null";

    out << ",\n"
        << "  \"winner_audio_path\": \"" << json_escape(path_utf8(report.winner_audio_path)) << "\",\n"
        << "  \"winner_seed\": " << report.winner_seed << ",\n"
        << "  \"ranking\": [\n";

    for (std::size_t rank = 0; rank < report.ranked.size(); ++rank) {
        const auto& score = report.ranked[rank];
        out << "    {\n"
            << "      \"rank\": " << (rank + 1) << ",\n"
            << "      \"candidate_index\": " << score.candidate_index << ",\n"
            << "      \"seed\": " << score.seed << ",\n"
            << "      \"eligible\": " << (score.eligible ? "true" : "false") << ",\n"
            << "      \"dna_available\": " << (score.dna_available ? "true" : "false") << ",\n"
            << "      \"total\": " << score.total << ",\n"
            << "      \"energy_match\": " << score.energy_match << ",\n"
            << "      \"bass_match\": " << score.bass_match << ",\n"
            << "      \"rhythm_match\": " << score.rhythm_match << ",\n"
            << "      \"spectral_match\": " << score.spectral_match << ",\n"
            << "      \"texture_match\": " << score.texture_match << ",\n"
            << "      \"provider\": \"" << json_escape(score.provider) << "\",\n"
            << "      \"audio_path\": \"" << json_escape(path_utf8(score.audio_path)) << "\",\n"
            << "      \"note\": \"" << json_escape(score.note) << "\"\n"
            << "    }" << (rank + 1 < report.ranked.size() ? "," : "") << "\n";
    }

    out << "  ]\n}\n";
    if (!out) throw std::runtime_error("EtherCritic failed while writing its ranking manifest");
}

} // namespace

bool CriticReport::has_winner() const noexcept {
    return winner_candidate_index.has_value();
}

CriticReport EtherCritic::rank(
    const DraftBatch& batch,
    const GenerationRequest& source_request,
    const std::filesystem::path& output_directory,
    CriticWeights weights) const {

    if (batch.candidates.empty()) {
        throw std::invalid_argument("EtherCritic requires a non-empty draft batch");
    }
    if (source_request.prompt.empty()) {
        throw std::invalid_argument("EtherCritic requires the source generation prompt");
    }

    std::filesystem::create_directories(output_directory);
    weights = normalize_weights(weights);

    EtherComposer composer;
    const CompositionPlan target = composer.compose(source_request);
    const double rhythm_target = clamp01((target.drum_density + target.transient_density) * 0.5);
    const double brightness_target = target_brightness(target);

    CriticReport report;
    report.batch_id = batch.batch_id;
    report.manifest_path = output_directory / (batch.batch_id + ".ethercritic.json");
    report.ranked.reserve(batch.candidates.size());

    for (const auto& candidate : batch.candidates) {
        CriticScore score;
        score.candidate_index = candidate.index;
        score.seed = candidate.seed;
        score.audio_path = candidate.artifact.audio_path;
        score.provider = candidate.artifact.backend_name;
        score.eligible = candidate.success && !candidate.artifact.audio_path.empty();

        if (!score.eligible) {
            score.note = candidate.error.empty() ? "candidate generation failed" : candidate.error;
            report.ranked.push_back(std::move(score));
            continue;
        }

        const auto dna = load_ether_dna_for_audio(candidate.artifact.audio_path);
        if (!dna) {
            score.note = "missing EtherDNA sidecar; analyze candidate before critic ranking";
            report.ranked.push_back(std::move(score));
            continue;
        }

        score.dna_available = true;
        score.energy_match = match01(dna->energy, target.energy);
        score.bass_match = match01(dna->low_end_weight, target.bass_weight);
        score.rhythm_match = match01(dna->rhythmic_activity, rhythm_target);
        score.spectral_match = match01(dna->brightness, brightness_target);
        score.texture_match = match01(texture_proxy(*dna), target.texture_grit);

        score.total = clamp01(
            score.energy_match * weights.energy +
            score.bass_match * weights.bass +
            score.rhythm_match * weights.rhythm +
            score.spectral_match * weights.spectral_character +
            score.texture_match * weights.texture);
        score.note = "scored from EtherDNA V0.1 against EtherComposer targets";
        report.ranked.push_back(std::move(score));
    }

    std::stable_sort(report.ranked.begin(), report.ranked.end(), [](const CriticScore& a, const CriticScore& b) {
        if (a.eligible != b.eligible) return a.eligible > b.eligible;
        if (a.dna_available != b.dna_available) return a.dna_available > b.dna_available;
        if (std::abs(a.total - b.total) > 1e-12) return a.total > b.total;
        return a.candidate_index < b.candidate_index;
    });

    const auto winner = std::find_if(report.ranked.begin(), report.ranked.end(), [](const CriticScore& score) {
        return score.eligible && score.dna_available;
    });
    if (winner != report.ranked.end()) {
        report.winner_candidate_index = winner->candidate_index;
        report.winner_audio_path = winner->audio_path;
        report.winner_seed = winner->seed;
    }

    write_manifest(report, target, weights);
    return report;
}

} // namespace etherbeat
