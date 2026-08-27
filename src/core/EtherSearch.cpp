#include "etherbeat/EtherSearch.hpp"

#include "etherbeat/EtherDNA.hpp"
#include "etherbeat/ModelRouter.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace etherbeat {
namespace {

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

void write_manifest(SearchReport& report) {
    std::ofstream out(report.manifest_path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("EtherSearch could not create its search manifest");

    out << "{\n"
        << "  \"schema\": \"etherbeat.search.v1\",\n"
        << "  \"search_id\": \"" << json_escape(report.search_id) << "\",\n"
        << "  \"draft_manifest\": \"" << json_escape(path_utf8(report.draft_batch.manifest_path)) << "\",\n"
        << "  \"critic_manifest\": \"" << json_escape(path_utf8(report.critic_report.manifest_path)) << "\",\n"
        << "  \"candidate_count\": " << report.candidates.size() << ",\n"
        << "  \"analyzed_count\": " << report.analyzed_count << ",\n"
        << "  \"dna_count\": " << report.dna_count << ",\n"
        << "  \"winner_candidate_index\": ";

    if (report.winner_candidate_index) out << *report.winner_candidate_index;
    else out << "null";

    out << ",\n"
        << "  \"winner_seed\": " << report.winner_seed << ",\n"
        << "  \"winner_audio_path\": \"" << json_escape(path_utf8(report.winner_audio_path)) << "\",\n"
        << "  \"candidates\": [\n";

    for (std::size_t i = 0; i < report.candidates.size(); ++i) {
        const auto& candidate = report.candidates[i];
        out << "    {\n"
            << "      \"candidate_index\": " << candidate.candidate_index << ",\n"
            << "      \"seed\": " << candidate.seed << ",\n"
            << "      \"generated\": " << (candidate.generated ? "true" : "false") << ",\n"
            << "      \"analysis_ready\": " << (candidate.analysis_ready ? "true" : "false") << ",\n"
            << "      \"dna_persisted\": " << (candidate.dna_persisted ? "true" : "false") << ",\n"
            << "      \"audio_path\": \"" << json_escape(path_utf8(candidate.audio_path)) << "\",\n"
            << "      \"analysis_error\": \"" << json_escape(candidate.analysis_error) << "\"\n"
            << "    }" << (i + 1 < report.candidates.size() ? "," : "") << "\n";
    }

    out << "  ]\n}\n";
    if (!out) throw std::runtime_error("EtherSearch failed while writing its search manifest");
}

} // namespace

bool SearchReport::has_winner() const noexcept {
    return winner_candidate_index.has_value();
}

SearchReport EtherSearch::run(
    ModelRouter& router,
    const GenerationRequest& request,
    const std::filesystem::path& output_directory,
    const AudioAnalyzer& analyzer,
    SearchOptions options) const {

    if (!analyzer) throw std::invalid_argument("EtherSearch requires an audio analyzer");
    if (request.prompt.empty()) throw std::invalid_argument("EtherSearch requires a generation prompt");

    std::filesystem::create_directories(output_directory);

    SearchReport report;
    EtherDraft draft;
    report.draft_batch = draft.generate(router, request, output_directory, options.draft);
    report.search_id = report.draft_batch.batch_id;
    report.manifest_path = output_directory / (report.search_id + ".ethersearch.json");
    report.candidates.reserve(report.draft_batch.candidates.size());

    for (const auto& candidate : report.draft_batch.candidates) {
        SearchCandidateState state;
        state.candidate_index = candidate.index;
        state.seed = candidate.seed;
        state.generated = candidate.success && !candidate.artifact.audio_path.empty();
        state.audio_path = candidate.artifact.audio_path;

        if (!state.generated) {
            state.analysis_error = candidate.error.empty() ? "candidate generation failed" : candidate.error;
            report.candidates.push_back(std::move(state));
            continue;
        }

        try {
            const AudioAnalysis analysis = analyzer(candidate.artifact.audio_path);
            if (!analysis.ready) {
                state.analysis_error = analysis.error.empty()
                    ? "audio analyzer returned no usable measurement"
                    : analysis.error;
            } else {
                state.analysis_ready = true;
                ++report.analyzed_count;

                const EtherDNA dna = make_ether_dna(candidate.artifact.audio_path, analysis);
                state.dna_persisted = save_ether_dna(
                    dna,
                    ether_dna_sidecar_path(candidate.artifact.audio_path));
                if (state.dna_persisted) {
                    ++report.dna_count;
                } else {
                    state.analysis_error = "analysis succeeded but EtherDNA sidecar could not be persisted";
                }
            }
        } catch (const std::exception& error) {
            state.analysis_error = error.what();
        } catch (...) {
            state.analysis_error = "audio analyzer threw an unknown error";
        }

        report.candidates.push_back(std::move(state));
    }

    EtherCritic critic;
    report.critic_report = critic.rank(
        report.draft_batch,
        request,
        output_directory,
        options.critic);

    if (report.critic_report.has_winner()) {
        report.winner_candidate_index = report.critic_report.winner_candidate_index;
        report.winner_audio_path = report.critic_report.winner_audio_path;
        report.winner_seed = report.critic_report.winner_seed;
    }

    write_manifest(report);
    return report;
}

} // namespace etherbeat
