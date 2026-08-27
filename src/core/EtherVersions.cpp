#include "etherbeat/EtherVersions.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace etherbeat {
namespace {

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string narrow_path(const fs::path& path) {
    return path.generic_string();
}

std::string json_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (const char c : input) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string json_unescape(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    bool escaped = false;
    for (const char c : input) {
        if (!escaped) {
            if (c == '\\') escaped = true;
            else out += c;
            continue;
        }
        escaped = false;
        switch (c) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case '\\': out += '\\'; break;
        case '"': out += '"'; break;
        default: out += c; break;
        }
    }
    if (escaped) out += '\\';
    return out;
}

std::string json_string(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = text.find(needle);
    if (pos == std::string::npos) return {};
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    pos = text.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    ++pos;
    std::string value;
    bool escaped = false;
    for (; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (!escaped && c == '"') break;
        if (!escaped && c == '\\') {
            escaped = true;
            value += c;
            continue;
        }
        escaped = false;
        value += c;
    }
    return json_unescape(value);
}

long long json_integer(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = text.find(needle);
    if (pos == std::string::npos) return 0;
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return 0;
    ++pos;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
    try {
        return std::stoll(text.substr(pos));
    } catch (...) {
        return 0;
    }
}

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_text(const fs::path& path, const std::string& text) {
    std::error_code ec;
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Could not write EtherVersions metadata: " + narrow_path(path));
    out << text;
}

std::string fnv_hex(const std::string& input) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char c : input) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return ss.str();
}

std::string canonical_key(const fs::path& path) {
    std::error_code ec;
    auto abs = fs::absolute(path, ec);
    if (ec) abs = path;
    return abs.lexically_normal().generic_string();
}

void save_record(const VersionRecord& record) {
    std::ostringstream out;
    out << "{\n"
        << "  \"schema\": \"" << json_escape(record.schema) << "\",\n"
        << "  \"version_id\": \"" << json_escape(record.version_id) << "\",\n"
        << "  \"root_id\": \"" << json_escape(record.root_id) << "\",\n"
        << "  \"parent_id\": \"" << json_escape(record.parent_id) << "\",\n"
        << "  \"audio_path\": \"" << json_escape(narrow_path(record.audio_path)) << "\",\n"
        << "  \"parent_audio_path\": \"" << json_escape(narrow_path(record.parent_audio_path)) << "\",\n"
        << "  \"operation\": \"" << json_escape(record.operation) << "\",\n"
        << "  \"instruction\": \"" << json_escape(record.instruction) << "\",\n"
        << "  \"created_unix_ms\": " << record.created_unix_ms << "\n"
        << "}\n";
    write_text(EtherVersions::sidecar_path(record.audio_path), out.str());
}

std::optional<VersionRecord> parse_record(const fs::path& sidecar) {
    const auto text = read_text(sidecar);
    if (text.empty()) return std::nullopt;
    VersionRecord record;
    record.schema = json_string(text, "schema");
    record.version_id = json_string(text, "version_id");
    record.root_id = json_string(text, "root_id");
    record.parent_id = json_string(text, "parent_id");
    record.audio_path = fs::path(json_string(text, "audio_path"));
    record.parent_audio_path = fs::path(json_string(text, "parent_audio_path"));
    record.operation = json_string(text, "operation");
    record.instruction = json_string(text, "instruction");
    record.created_unix_ms = json_integer(text, "created_unix_ms");
    if (record.version_id.empty() || record.root_id.empty() || record.audio_path.empty()) {
        return std::nullopt;
    }
    return record;
}

} // namespace

EtherVersions::EtherVersions(fs::path library_root)
    : library_root_(std::move(library_root)) {}

fs::path EtherVersions::sidecar_path(const fs::path& audio_path) {
    fs::path result = audio_path;
    result += ".etherversion.json";
    return result;
}

fs::path EtherVersions::heads_root() const {
    return library_root_ / ".etherversions" / "heads";
}

fs::path EtherVersions::head_path(const std::string& root_id) const {
    return heads_root() / (root_id + ".head");
}

std::string EtherVersions::read_head(const std::string& root_id) const {
    auto value = read_text(head_path(root_id));
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
}

void EtherVersions::write_head(const std::string& root_id, const std::string& version_id) const {
    write_text(head_path(root_id), version_id + "\n");
}

std::optional<VersionRecord> EtherVersions::load_for_audio(const fs::path& audio_path) const {
    return parse_record(sidecar_path(audio_path));
}

VersionRecord EtherVersions::ensure_root(const fs::path& audio_path) {
    if (audio_path.empty()) throw std::invalid_argument("EtherVersions root audio path is empty");
    if (auto existing = load_for_audio(audio_path)) return *existing;

    VersionRecord record;
    record.root_id = fnv_hex("root|" + canonical_key(audio_path));
    record.version_id = record.root_id + "-root";
    record.audio_path = audio_path;
    record.operation = "root";
    record.created_unix_ms = now_ms();
    save_record(record);
    if (read_head(record.root_id).empty()) write_head(record.root_id, record.version_id);
    return record;
}

VersionRecord EtherVersions::register_child(
    const fs::path& parent_audio_path,
    const fs::path& child_audio_path,
    std::string operation,
    std::string instruction) {

    if (child_audio_path.empty()) throw std::invalid_argument("EtherVersions child audio path is empty");
    auto parent = ensure_root(parent_audio_path);
    if (auto existing = load_for_audio(child_audio_path)) return *existing;

    VersionRecord child;
    child.root_id = parent.root_id;
    child.parent_id = parent.version_id;
    child.audio_path = child_audio_path;
    child.parent_audio_path = parent.audio_path;
    child.operation = std::move(operation);
    child.instruction = std::move(instruction);
    child.created_unix_ms = now_ms();
    child.version_id = child.root_id + "-" + fnv_hex(
        child.parent_id + "|" + canonical_key(child.audio_path) + "|" +
        child.operation + "|" + std::to_string(child.created_unix_ms));
    save_record(child);
    return child;
}

std::vector<VersionRecord> EtherVersions::scan_records() const {
    std::vector<VersionRecord> records;
    std::error_code ec;
    if (!fs::exists(library_root_, ec)) return records;
    for (const auto& entry : fs::recursive_directory_iterator(
             library_root_, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const auto filename = entry.path().filename().string();
        if (filename.size() < 18 || filename.rfind(".etherversion.json") != filename.size() - 18) continue;
        if (auto record = parse_record(entry.path())) records.push_back(*record);
    }
    return records;
}

VersionLineage EtherVersions::lineage_for_audio(const fs::path& audio_path) const {
    auto current = load_for_audio(audio_path);
    if (!current) throw std::runtime_error("Track has no EtherVersions lineage yet");

    VersionLineage lineage;
    lineage.current = *current;
    lineage.promoted_version_id = read_head(current->root_id);
    for (const auto& record : scan_records()) {
        if (record.root_id != current->root_id) continue;
        if (!current->parent_id.empty() && record.version_id == current->parent_id) lineage.parent = record;
        if (record.parent_id == current->version_id) lineage.children.push_back(record);
    }
    return lineage;
}

void EtherVersions::promote(const fs::path& audio_path) {
    auto record = load_for_audio(audio_path);
    if (!record) throw std::runtime_error("Cannot promote track without EtherVersions lineage");
    write_head(record->root_id, record->version_id);
}

fs::path EtherVersions::promoted_audio_for(const fs::path& audio_path) const {
    auto record = load_for_audio(audio_path);
    if (!record) return {};
    const auto head = read_head(record->root_id);
    if (head.empty()) return {};
    for (const auto& candidate : scan_records()) {
        if (candidate.root_id == record->root_id && candidate.version_id == head) {
            return candidate.audio_path;
        }
    }
    return {};
}

fs::path EtherVersions::parent_audio_for(const fs::path& audio_path) const {
    auto record = load_for_audio(audio_path);
    if (!record) return {};
    return record->parent_audio_path;
}

} // namespace etherbeat
