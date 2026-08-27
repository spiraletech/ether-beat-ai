#include "etherbeat/EtherSections.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>

namespace etherbeat {
namespace {

constexpr std::array<double, 6> kDefaultBoundaries{0.0, 0.12, 0.36, 0.62, 0.84, 1.0};
constexpr std::array<SectionKind, 5> kKinds{
    SectionKind::Intro,
    SectionKind::Verse,
    SectionKind::Hook,
    SectionKind::Bridge,
    SectionKind::Outro};

std::string path_utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string escape_json(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: if (c >= 0x20) out.push_back(static_cast<char>(c)); break;
        }
    }
    return out;
}

std::optional<double> number_after(const std::string& text, const std::string& key, std::size_t from = 0) {
    const std::string needle = "\"" + key + "\"";
    const auto keyPos = text.find(needle, from);
    if (keyPos == std::string::npos) return std::nullopt;
    const auto colon = text.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return std::nullopt;
    const char* begin = text.c_str() + colon + 1;
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<std::string> string_after(const std::string& text, const std::string& key, std::size_t from = 0) {
    const std::string needle = "\"" + key + "\"";
    const auto keyPos = text.find(needle, from);
    if (keyPos == std::string::npos) return std::nullopt;
    const auto colon = text.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return std::nullopt;
    const auto quote = text.find('"', colon + 1);
    if (quote == std::string::npos) return std::nullopt;
    std::string out;
    bool escaped = false;
    for (std::size_t i = quote + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (escaped) {
            switch (c) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: out.push_back(c); break;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            return out;
        } else {
            out.push_back(c);
        }
    }
    return std::nullopt;
}

SectionKind parse_kind(const std::string& value) {
    if (value == "INTRO") return SectionKind::Intro;
    if (value == "HOOK") return SectionKind::Hook;
    if (value == "BRIDGE") return SectionKind::Bridge;
    if (value == "OUTRO") return SectionKind::Outro;
    return SectionKind::Verse;
}

std::size_t valley_near(const AudioAnalysis& analysis, double normalizedTarget) {
    const auto& envelope = analysis.timeline_envelope;
    if (envelope.empty()) return 0;
    const std::size_t last = envelope.size() - 1;
    const std::size_t center = static_cast<std::size_t>(std::llround(
        std::clamp(normalizedTarget, 0.0, 1.0) * static_cast<double>(last)));
    constexpr std::size_t radius = 7;
    const std::size_t begin = center > radius ? center - radius : 0;
    const std::size_t end = std::min(last, center + radius);

    std::size_t best = center;
    float bestScore = envelope[center];
    for (std::size_t i = begin; i <= end; ++i) {
        const double distance = std::abs(static_cast<double>(i) - static_cast<double>(center)) /
                                static_cast<double>(std::max<std::size_t>(1, radius));
        const float score = envelope[i] + static_cast<float>(distance * 0.08);
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

float boundary_confidence(const AudioAnalysis& analysis, std::size_t index) {
    const auto& e = analysis.timeline_envelope;
    if (e.empty() || index >= e.size()) return 0.35f;
    const std::size_t lo = index > 3 ? index - 3 : 0;
    const std::size_t hi = std::min(e.size() - 1, index + 3);
    float neighborhood = 0.0f;
    int count = 0;
    for (std::size_t i = lo; i <= hi; ++i) {
        if (i == index) continue;
        neighborhood += e[i];
        ++count;
    }
    if (count == 0) return 0.35f;
    neighborhood /= static_cast<float>(count);
    const float valley = std::clamp(neighborhood - e[index], 0.0f, 1.0f);
    return std::clamp(0.45f + valley * 0.55f, 0.0f, 1.0f);
}

} // namespace

const char* section_kind_name(SectionKind kind) noexcept {
    switch (kind) {
    case SectionKind::Intro: return "INTRO";
    case SectionKind::Verse: return "VERSE";
    case SectionKind::Hook: return "HOOK";
    case SectionKind::Bridge: return "BRIDGE";
    case SectionKind::Outro: return "OUTRO";
    }
    return "VERSE";
}

SectionMap infer_sections(const std::filesystem::path& source_audio, const AudioAnalysis& analysis) noexcept {
    SectionMap map;
    map.source_audio = source_audio;
    map.duration_seconds = std::max(0.0, analysis.duration_seconds);
    if (!analysis.ready || map.duration_seconds <= 0.0) return map;

    std::array<double, 6> boundaries{};
    boundaries.front() = 0.0;
    boundaries.back() = map.duration_seconds;

    const auto envelopeCount = analysis.timeline_envelope.size();
    for (std::size_t i = 1; i + 1 < boundaries.size(); ++i) {
        const std::size_t valley = valley_near(analysis, kDefaultBoundaries[i]);
        const double normalized = envelopeCount > 1
            ? static_cast<double>(valley) / static_cast<double>(envelopeCount - 1)
            : kDefaultBoundaries[i];
        boundaries[i] = std::clamp(normalized * map.duration_seconds, 0.0, map.duration_seconds);
    }

    const double minSection = std::min(2.0, std::max(0.25, map.duration_seconds / 20.0));
    for (std::size_t i = 1; i + 1 < boundaries.size(); ++i) {
        const double minAllowed = boundaries[i - 1] + minSection;
        const double remaining = static_cast<double>(boundaries.size() - 1 - i) * minSection;
        const double maxAllowed = std::max(minAllowed, map.duration_seconds - remaining);
        boundaries[i] = std::clamp(boundaries[i], minAllowed, maxAllowed);
    }

    map.sections.reserve(kKinds.size());
    for (std::size_t i = 0; i < kKinds.size(); ++i) {
        float confidence = 0.65f;
        if (i > 0 && i < kKinds.size() && envelopeCount > 1) {
            const double n = boundaries[i] / map.duration_seconds;
            const std::size_t index = static_cast<std::size_t>(std::llround(
                n * static_cast<double>(envelopeCount - 1)));
            confidence = boundary_confidence(analysis, index);
        }
        map.sections.push_back(SongSection{
            .kind = kKinds[i],
            .label = section_kind_name(kKinds[i]),
            .start_seconds = boundaries[i],
            .end_seconds = boundaries[i + 1],
            .confidence = confidence});
    }
    return map;
}

std::optional<SongSection> find_section(const SectionMap& map, SectionKind kind) noexcept {
    const auto it = std::find_if(map.sections.begin(), map.sections.end(), [kind](const SongSection& section) {
        return section.kind == kind;
    });
    if (it == map.sections.end()) return std::nullopt;
    return *it;
}

TimelineSelection section_selection(const SectionMap& map, SectionKind kind) noexcept {
    const auto section = find_section(map, kind);
    if (!section) return {};
    return TimelineSelection{
        .start_seconds = section->start_seconds,
        .end_seconds = section->end_seconds,
        .duration_seconds = map.duration_seconds};
}

TimelineSelection snap_selection_to_section(const SectionMap& map, const TimelineSelection& selection) noexcept {
    if (!selection.valid() || map.sections.empty()) return selection;
    const double midpoint = (selection.start_seconds + selection.end_seconds) * 0.5;
    const auto it = std::min_element(map.sections.begin(), map.sections.end(), [midpoint](const SongSection& a, const SongSection& b) {
        const double amid = (a.start_seconds + a.end_seconds) * 0.5;
        const double bmid = (b.start_seconds + b.end_seconds) * 0.5;
        return std::abs(amid - midpoint) < std::abs(bmid - midpoint);
    });
    if (it == map.sections.end()) return selection;
    return TimelineSelection{it->start_seconds, it->end_seconds, map.duration_seconds};
}

std::filesystem::path ether_sections_sidecar_path(const std::filesystem::path& audio_path) {
    return std::filesystem::path(audio_path.wstring() + L".ethersections.json");
}

bool save_section_map(const SectionMap& map, const std::filesystem::path& path) noexcept {
    try {
        if (path.empty()) return false;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << std::fixed << std::setprecision(6);
        out << "{\n  \"schema\": \"etherbeat.sections.v1\",\n";
        out << "  \"source_audio\": \"" << escape_json(path_utf8(map.source_audio)) << "\",\n";
        out << "  \"duration_seconds\": " << map.duration_seconds << ",\n";
        out << "  \"sections\": [\n";
        for (std::size_t i = 0; i < map.sections.size(); ++i) {
            const auto& s = map.sections[i];
            out << "    {\"kind\": \"" << section_kind_name(s.kind)
                << "\", \"label\": \"" << escape_json(s.label)
                << "\", \"start\": " << s.start_seconds
                << ", \"end\": " << s.end_seconds
                << ", \"confidence\": " << s.confidence << "}";
            if (i + 1 < map.sections.size()) out << ',';
            out << '\n';
        }
        out << "  ]\n}\n";
        return static_cast<bool>(out);
    } catch (...) {
        return false;
    }
}

std::optional<SectionMap> load_section_map(const std::filesystem::path& path) noexcept {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        const std::string text(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
        const auto schema = string_after(text, "schema");
        const auto duration = number_after(text, "duration_seconds");
        if (!schema || *schema != "etherbeat.sections.v1" || !duration || *duration <= 0.0) return std::nullopt;

        SectionMap map;
        map.schema = *schema;
        map.duration_seconds = *duration;
        if (const auto source = string_after(text, "source_audio")) map.source_audio = std::filesystem::u8path(*source);

        std::size_t cursor = text.find("\"sections\"");
        while (cursor != std::string::npos) {
            const auto objectStart = text.find('{', cursor);
            if (objectStart == std::string::npos) break;
            const auto objectEnd = text.find('}', objectStart);
            if (objectEnd == std::string::npos) break;
            const std::string object = text.substr(objectStart, objectEnd - objectStart + 1);
            const auto kind = string_after(object, "kind");
            const auto label = string_after(object, "label");
            const auto start = number_after(object, "start");
            const auto end = number_after(object, "end");
            const auto confidence = number_after(object, "confidence");
            if (kind && label && start && end && confidence && *end > *start) {
                map.sections.push_back(SongSection{
                    .kind = parse_kind(*kind),
                    .label = *label,
                    .start_seconds = *start,
                    .end_seconds = *end,
                    .confidence = std::clamp(static_cast<float>(*confidence), 0.0f, 1.0f)});
            }
            cursor = objectEnd + 1;
            if (map.sections.size() >= 5) break;
        }
        if (map.sections.empty()) return std::nullopt;
        return map;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<SectionMap> load_section_map_for_audio(const std::filesystem::path& audio_path) noexcept {
    if (audio_path.empty()) return std::nullopt;
    return load_section_map(ether_sections_sidecar_path(audio_path));
}

} // namespace etherbeat
