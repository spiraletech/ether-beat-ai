#include "etherbeat/EtherPromote.hpp"

#include "etherbeat/EtherDNA.hpp"
#include "etherbeat/ModelRouter.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace etherbeat {
namespace {

double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

double match01(double a, double b) {
    return clamp01(1.0 - std::abs(clamp01(a) - clamp01(b)));
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

double preservation_score(const EtherDNA& source, const EtherDNA& promoted) {
    return clamp01((
        match01(source.energy, promoted.energy) +
        match01(source.low_end_weight, promoted.low_end_weight) +
        match01(source.rhythmic_activity, promoted.rhythmic_activity) +
        match01(source.brightness, promoted.brightness) +
        match01(source.spectral_center, promoted.spectral_center)) / 5.0);
}

void write_manifest(const PromotionReport& report) {
    std::ofstream out(report.manifest_path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("EtherPromote could not create its promotion manifest");

    out << std::fixed << std::setprecision(6)
        << "{\n"
        << "  \"schema\": \"etherbeat.promote.v1\",\n"
        << "  \"promotion_id\": \"" << json_escape(report.promotion_id) << "\",\n"
        << "  \"source_candidate_index\": " << report.source_candidate_index << ",\n"
        << "  \"source_seed\": " << report.source_seed << ",\n"
        << "  \"source_audio_path\": \"" << json_escape(path_utf8(report.source_audio_path)) << "\",\n"
        << "  \"source_dna_available\": " << (report.source_dna_available ? "true" : "false") << ",\n"
        << "  \"quality_generated\": " << (report.quality_generated ? "true" : "false") << ",\n"
        << "  \"quality_analysis_ready\": " << (report.quality_analysis_ready ? "true" : "false") << ",\n"
        << "  \"quality_dna_persisted\": " << (report.quality_dna_persisted ? "true" : "false") << ",\n"
        << "  \"quality_backend\": \"" << json_escape(report.quality_artifact.backend_name) << "\",\n"
        << "  \"quality_seed\": " << report.quality_artifact.resolved_seed << ",\n"
        << "  \"quality_audio_path\": \"" << json_escape(path_utf8(report.quality_artifact.audio_path)) << "\",\n"
        << "  \"quality_metadata_path\": \"" << json_escape(path_utf8(report.quality_artifact.metadata_path)) << "\",\n"
        << "  \"dna_preservation_score\": " << report.dna_preservation_score << ",\n"
        << "  \"error\": \"" << json_escape(report.error) << "\"\n"
        << "}\n";

    if (!out) throw std::runtime_error("EtherPromote failed while writing its promotion manifest");
}

} // namespace

bool PromotionReport::succeeded() const noexcept {
    return quality_generated && quality_analysis_ready && quality_dna_persisted && error.empty();
}

PromotionReport EtherPromote::run(
    ModelRouter& router,
    const SearchReport& search,
    const GenerationRequest& source_request,
    const std::filesystem::path& output_directory,
    const AudioAnalyzer& analyzer) const {

    if (!search.has_winner() || !search.winner_candidate_index) {
        throw std::invalid_argument("EtherPromote requires an EtherSearch winner");
    }
    if (!analyzer) throw std::invalid_argument("EtherPromote requires an audio analyzer");
    if (source_request.prompt.empty()) throw std::invalid_argument("EtherPromote requires the source prompt");

    PromotionReport report;
    report.promotion_id = search.search_id + "-quality";
    report.source_candidate_index = *search.winner_candidate_index;
    report.source_seed = search.winner_seed;
    report.source_audio_path = search.winner_audio_path;
    report.manifest_path = output_directory / (report.promotion_id + ".etherpromote.json");

    const auto source_dna = load_ether_dna_for_audio(report.source_audio_path);
    report.source_dna_available = source_dna.has_value();
    if (!source_dna) {
        report.error = "winning draft has no EtherDNA sidecar; refusing ungrounded Quality promotion";
        write_manifest(report);
        return report;
    }

    GenerationRequest quality_request = source_request;
    quality_request.render_intent = RenderIntent::Quality;
    quality_request.mode = GenerationMode::TextToInstrumental;
    quality_request.reference_audio.clear();
    quality_request.seed = report.source_seed;
    quality_request.prompt += ". Quality promotion target from winning draft. Preserve the same musical identity and seed lineage. Measured ";
    quality_request.prompt += source_dna->conditioning_summary();
    quality_request.prompt += ". Treat these measurements as preservation targets while increasing fidelity, detail and mix coherence; do not introduce an unrelated arrangement.";

    const auto promoted_dir = output_directory / "promoted" / search.search_id;
    std::filesystem::create_directories(promoted_dir);

    try {
        report.quality_artifact = router.generate(quality_request, promoted_dir);
        report.quality_generated = !report.quality_artifact.audio_path.empty();
        if (!report.quality_generated) {
            report.error = "Quality provider returned no audio artifact";
            write_manifest(report);
            return report;
        }

        report.quality_analysis = analyzer(report.quality_artifact.audio_path);
        if (!report.quality_analysis.ready) {
            report.error = report.quality_analysis.error.empty()
                ? "Quality render could not be analyzed"
                : report.quality_analysis.error;
            write_manifest(report);
            return report;
        }
        report.quality_analysis_ready = true;

        const EtherDNA promoted_dna = make_ether_dna(report.quality_artifact.audio_path, report.quality_analysis);
        report.quality_dna_persisted = save_ether_dna(
            promoted_dna,
            ether_dna_sidecar_path(report.quality_artifact.audio_path));
        if (!report.quality_dna_persisted) {
            report.error = "Quality render was analyzed but its EtherDNA sidecar could not be persisted";
            write_manifest(report);
            return report;
        }

        report.dna_preservation_score = preservation_score(*source_dna, promoted_dna);
    } catch (const std::exception& error) {
        report.error = error.what();
    } catch (...) {
        report.error = "Quality promotion failed with an unknown error";
    }

    write_manifest(report);
    return report;
}

} // namespace etherbeat
