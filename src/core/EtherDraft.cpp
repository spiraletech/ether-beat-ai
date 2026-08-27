#include "etherbeat/EtherDraft.hpp"

#include "etherbeat/ModelRouter.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace etherbeat {
namespace {

std::uint64_t resolve_base_seed(std::uint64_t requested) {
    if (requested != 0) return requested;

    std::random_device random_device;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return (static_cast<std::uint64_t>(random_device()) << 32u) ^ now;
}

std::uint64_t splitmix64(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31u);
}

std::uint64_t candidate_seed(std::uint64_t base, std::size_t index) {
    if (index == 0) return base;
    return splitmix64(base + static_cast<std::uint64_t>(index));
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

std::string make_batch_id(std::uint64_t base_seed) {
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ostringstream out;
    out << "draft-" << std::hex << base_seed << "-" << std::dec << stamp;
    return out.str();
}

void write_manifest(const DraftBatch& batch, const GenerationRequest& source_request) {
    std::ofstream out(batch.manifest_path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("EtherDraft could not create its batch manifest");

    out << "{\n"
        << "  \"schema\": \"etherbeat.draft.v1\",\n"
        << "  \"batch_id\": \"" << json_escape(batch.batch_id) << "\",\n"
        << "  \"base_seed\": " << batch.base_seed << ",\n"
        << "  \"prompt\": \"" << json_escape(source_request.prompt) << "\",\n"
        << "  \"requested_count\": " << batch.candidates.size() << ",\n"
        << "  \"success_count\": " << batch.success_count() << ",\n"
        << "  \"candidates\": [\n";

    for (std::size_t i = 0; i < batch.candidates.size(); ++i) {
        const auto& candidate = batch.candidates[i];
        out << "    {\n"
            << "      \"index\": " << candidate.index << ",\n"
            << "      \"seed\": " << candidate.seed << ",\n"
            << "      \"success\": " << (candidate.success ? "true" : "false") << ",\n"
            << "      \"provider\": \"" << json_escape(candidate.artifact.backend_name) << "\",\n"
            << "      \"audio_path\": \"" << json_escape(candidate.artifact.audio_path.string()) << "\",\n"
            << "      \"metadata_path\": \"" << json_escape(candidate.artifact.metadata_path.string()) << "\",\n"
            << "      \"error\": \"" << json_escape(candidate.error) << "\"\n"
            << "    }" << (i + 1 < batch.candidates.size() ? "," : "") << "\n";
    }

    out << "  ]\n}\n";
    if (!out) throw std::runtime_error("EtherDraft failed while writing its batch manifest");
}

} // namespace

std::size_t DraftBatch::success_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        candidates.begin(), candidates.end(), [](const DraftCandidate& candidate) {
            return candidate.success;
        }));
}

bool DraftBatch::has_success() const noexcept {
    return success_count() != 0;
}

DraftBatch EtherDraft::generate(
    ModelRouter& router,
    const GenerationRequest& request,
    const std::filesystem::path& output_directory,
    DraftOptions options) const {

    if (request.prompt.empty()) throw std::invalid_argument("EtherDraft requires a generation prompt");
    if (request.mode != GenerationMode::TextToInstrumental) {
        throw std::invalid_argument("EtherDraft V0.1 currently supports text-to-instrumental batches only");
    }

    options.candidate_count = std::clamp<std::size_t>(options.candidate_count, 1, 8);
    std::filesystem::create_directories(output_directory);

    DraftBatch batch;
    batch.base_seed = resolve_base_seed(request.seed);
    batch.batch_id = make_batch_id(batch.base_seed);
    batch.manifest_path = output_directory / (batch.batch_id + ".etherdraft.json");
    batch.candidates.reserve(options.candidate_count);

    for (std::size_t index = 0; index < options.candidate_count; ++index) {
        DraftCandidate candidate;
        candidate.index = index;
        candidate.seed = candidate_seed(batch.base_seed, index);

        GenerationRequest candidate_request = request;
        candidate_request.seed = candidate.seed;
        candidate_request.render_intent = RenderIntent::Draft;

        try {
            candidate.artifact = router.generate(candidate_request, output_directory);
            candidate.success = true;
        } catch (const std::exception& error) {
            candidate.error = error.what();
            candidate.success = false;
        }

        batch.candidates.push_back(std::move(candidate));
        if (!batch.candidates.back().success && !options.continue_after_failure) break;
    }

    write_manifest(batch, request);
    return batch;
}

} // namespace etherbeat
