#include "etherbeat/EtherArrangement.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <utility>

namespace etherbeat {
namespace {

std::string escape_json(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
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

std::string unescape_json(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool escaped = false;
    for (const char c : value) {
        if (escaped) {
            switch (c) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            default: out += c; break;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else {
            out += c;
        }
    }
    if (escaped) out += '\\';
    return out;
}

std::optional<std::string> string_after(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const auto keyPos = text.find(needle);
    if (keyPos == std::string::npos) return std::nullopt;
    const auto colon = text.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return std::nullopt;
    const auto open = text.find('"', colon + 1);
    if (open == std::string::npos) return std::nullopt;

    std::string raw;
    bool escaped = false;
    for (std::size_t i = open + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (!escaped && c == '"') return unescape_json(raw);
        raw += c;
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
    }
    return std::nullopt;
}

std::optional<double> number_after(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const auto keyPos = text.find(needle);
    if (keyPos == std::string::npos) return std::nullopt;
    const auto colon = text.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return std::nullopt;
    std::size_t start = colon + 1;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    std::size_t end = start;
    while (end < text.size()) {
        const char c = text[end];
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')) break;
        ++end;
    }
    if (end == start) return std::nullopt;
    try {
        return std::stod(text.substr(start, end - start));
    } catch (...) {
        return std::nullopt;
    }
}

std::string path_utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::filesystem::path path_from_utf8(const std::string& value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const unsigned char c : value) utf8.push_back(static_cast<char8_t>(c));
    return std::filesystem::path(utf8);
}

SectionKind parse_kind(const std::string& value) noexcept {
    if (value == "INTRO") return SectionKind::Intro;
    if (value == "HOOK") return SectionKind::Hook;
    if (value == "BRIDGE") return SectionKind::Bridge;
    if (value == "OUTRO") return SectionKind::Outro;
    return SectionKind::Verse;
}

ArrangementOrigin parse_origin(const std::string& value) noexcept {
    if (value == "duplicate") return ArrangementOrigin::Duplicate;
    if (value == "placeholder") return ArrangementOrigin::Placeholder;
    return ArrangementOrigin::Source;
}

std::string make_slot_id(std::uint64_t value) {
    std::ostringstream out;
    out << "slot-" << std::setfill('0') << std::setw(4) << value;
    return out.str();
}

std::string next_slot_id(const ArrangementPlan& plan) {
    std::uint64_t maximum = 0;
    for (const auto& slot : plan.slots) {
        if (slot.slot_id.rfind("slot-", 0) != 0) continue;
        try {
            maximum = std::max(maximum, static_cast<std::uint64_t>(std::stoull(slot.slot_id.substr(5))));
        } catch (...) {}
    }
    return make_slot_id(maximum + 1);
}

} // namespace

const char* arrangement_origin_name(ArrangementOrigin origin) noexcept {
    switch (origin) {
    case ArrangementOrigin::Source: return "source";
    case ArrangementOrigin::Duplicate: return "duplicate";
    case ArrangementOrigin::Placeholder: return "placeholder";
    }
    return "source";
}

ArrangementPlan make_arrangement_plan(const SectionMap& sections) {
    ArrangementPlan plan;
    plan.source_audio = sections.source_audio;
    plan.slots.reserve(sections.sections.size());
    std::uint64_t id = 1;
    for (const auto& section : sections.sections) {
        plan.slots.push_back(ArrangementSlot{
            .slot_id = make_slot_id(id++),
            .kind = section.kind,
            .label = section.label.empty() ? section_kind_name(section.kind) : section.label,
            .origin = ArrangementOrigin::Source,
            .origin_slot_id = {},
            .source_start_seconds = section.start_seconds,
            .source_end_seconds = section.end_seconds,
            .instruction = {}});
    }
    return plan;
}

bool duplicate_arrangement_slot(ArrangementPlan& plan, std::size_t index, bool insert_after) {
    if (index >= plan.slots.size()) return false;
    ArrangementSlot copy = plan.slots[index];
    const std::string sourceId = copy.slot_id;
    copy.slot_id = next_slot_id(plan);
    copy.origin = ArrangementOrigin::Duplicate;
    copy.origin_slot_id = sourceId;
    const std::size_t destination = insert_after ? index + 1 : index;
    plan.slots.insert(plan.slots.begin() + static_cast<std::ptrdiff_t>(destination), std::move(copy));
    ++plan.revision;
    return true;
}

bool move_arrangement_slot(ArrangementPlan& plan, std::size_t from_index, std::size_t to_index) {
    if (from_index >= plan.slots.size() || to_index >= plan.slots.size() || from_index == to_index) return false;
    ArrangementSlot slot = std::move(plan.slots[from_index]);
    plan.slots.erase(plan.slots.begin() + static_cast<std::ptrdiff_t>(from_index));
    plan.slots.insert(plan.slots.begin() + static_cast<std::ptrdiff_t>(to_index), std::move(slot));
    ++plan.revision;
    return true;
}

bool insert_arrangement_placeholder(
    ArrangementPlan& plan,
    std::size_t index,
    SectionKind kind,
    std::string label,
    std::string instruction) {

    if (index > plan.slots.size()) return false;
    if (label.empty()) label = std::string(section_kind_name(kind)) + " NEW";
    ArrangementSlot slot{
        .slot_id = next_slot_id(plan),
        .kind = kind,
        .label = std::move(label),
        .origin = ArrangementOrigin::Placeholder,
        .origin_slot_id = {},
        .source_start_seconds = -1.0,
        .source_end_seconds = -1.0,
        .instruction = std::move(instruction)};
    plan.slots.insert(plan.slots.begin() + static_cast<std::ptrdiff_t>(index), std::move(slot));
    ++plan.revision;
    return true;
}

bool erase_arrangement_slot(ArrangementPlan& plan, std::size_t index) {
    if (index >= plan.slots.size() || plan.slots.size() <= 1) return false;
    plan.slots.erase(plan.slots.begin() + static_cast<std::ptrdiff_t>(index));
    ++plan.revision;
    return true;
}

std::string arrangement_blueprint(const ArrangementPlan& plan) {
    std::ostringstream out;
    out << "Arrangement revision " << plan.revision << ": ";
    for (std::size_t i = 0; i < plan.slots.size(); ++i) {
        const auto& slot = plan.slots[i];
        if (i) out << " -> ";
        out << '[' << (i + 1) << ' ' << slot.label << ' ' << arrangement_origin_name(slot.origin);
        if (slot.has_source_audio()) {
            out << ' ' << std::fixed << std::setprecision(2)
                << slot.source_start_seconds << '-' << slot.source_end_seconds << 's';
        }
        if (!slot.origin_slot_id.empty()) out << " from=" << slot.origin_slot_id;
        if (!slot.instruction.empty()) out << " instruction=\"" << slot.instruction << '\"';
        out << ']';
    }
    return out.str();
}

std::filesystem::path ether_arrangement_sidecar_path(const std::filesystem::path& audio_path) {
    return std::filesystem::path(audio_path.wstring() + L".etherarrangement.json");
}

bool save_arrangement_plan(const ArrangementPlan& plan, const std::filesystem::path& path) noexcept {
    try {
        if (path.empty()) return false;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << std::fixed << std::setprecision(6);
        out << "{\n  \"schema\": \"etherbeat.arrangement.v1\",\n";
        out << "  \"source_audio\": \"" << escape_json(path_utf8(plan.source_audio)) << "\",\n";
        out << "  \"revision\": " << plan.revision << ",\n";
        out << "  \"slots\": [\n";
        for (std::size_t i = 0; i < plan.slots.size(); ++i) {
            const auto& slot = plan.slots[i];
            out << "    {\"slot_id\": \"" << escape_json(slot.slot_id)
                << "\", \"kind\": \"" << section_kind_name(slot.kind)
                << "\", \"label\": \"" << escape_json(slot.label)
                << "\", \"origin\": \"" << arrangement_origin_name(slot.origin)
                << "\", \"origin_slot_id\": \"" << escape_json(slot.origin_slot_id)
                << "\", \"start\": " << slot.source_start_seconds
                << ", \"end\": " << slot.source_end_seconds
                << ", \"instruction\": \"" << escape_json(slot.instruction) << "\"}";
            if (i + 1 < plan.slots.size()) out << ',';
            out << '\n';
        }
        out << "  ]\n}\n";
        return static_cast<bool>(out);
    } catch (...) {
        return false;
    }
}

std::optional<ArrangementPlan> load_arrangement_plan(const std::filesystem::path& path) noexcept {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        const std::string text(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
        const auto schema = string_after(text, "schema");
        const auto revision = number_after(text, "revision");
        if (!schema || *schema != "etherbeat.arrangement.v1" || !revision || *revision < 0.0) return std::nullopt;

        ArrangementPlan plan;
        plan.schema = *schema;
        plan.revision = static_cast<std::uint64_t>(std::llround(*revision));
        if (const auto source = string_after(text, "source_audio")) plan.source_audio = path_from_utf8(*source);

        std::size_t cursor = text.find("\"slots\"");
        while (cursor != std::string::npos) {
            const auto objectStart = text.find('{', cursor);
            if (objectStart == std::string::npos) break;
            const auto objectEnd = text.find('}', objectStart);
            if (objectEnd == std::string::npos) break;
            const std::string object = text.substr(objectStart, objectEnd - objectStart + 1);
            const auto slotId = string_after(object, "slot_id");
            const auto kind = string_after(object, "kind");
            const auto label = string_after(object, "label");
            const auto origin = string_after(object, "origin");
            const auto originId = string_after(object, "origin_slot_id");
            const auto start = number_after(object, "start");
            const auto end = number_after(object, "end");
            const auto instruction = string_after(object, "instruction");
            if (slotId && kind && label && origin && originId && start && end && instruction) {
                plan.slots.push_back(ArrangementSlot{
                    .slot_id = *slotId,
                    .kind = parse_kind(*kind),
                    .label = *label,
                    .origin = parse_origin(*origin),
                    .origin_slot_id = *originId,
                    .source_start_seconds = *start,
                    .source_end_seconds = *end,
                    .instruction = *instruction});
            }
            cursor = objectEnd + 1;
        }
        if (plan.slots.empty()) return std::nullopt;
        return plan;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<ArrangementPlan> load_arrangement_plan_for_audio(const std::filesystem::path& audio_path) noexcept {
    if (audio_path.empty()) return std::nullopt;
    return load_arrangement_plan(ether_arrangement_sidecar_path(audio_path));
}

} // namespace etherbeat
