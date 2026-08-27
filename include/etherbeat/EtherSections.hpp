#pragma once

#include "etherbeat/AudioAnalysis.hpp"
#include "etherbeat/EtherTimeline.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace etherbeat {

enum class SectionKind {
    Intro,
    Verse,
    Hook,
    Bridge,
    Outro
};

struct SongSection {
    SectionKind kind{SectionKind::Verse};
    std::string label;
    double start_seconds{0.0};
    double end_seconds{0.0};
    float confidence{0.0f};

    [[nodiscard]] double length_seconds() const noexcept {
        return end_seconds - start_seconds;
    }
};

struct SectionMap {
    std::string schema{"etherbeat.sections.v1"};
    std::filesystem::path source_audio;
    double duration_seconds{0.0};
    std::vector<SongSection> sections;
};

[[nodiscard]] const char* section_kind_name(SectionKind kind) noexcept;

[[nodiscard]] SectionMap infer_sections(
    const std::filesystem::path& source_audio,
    const AudioAnalysis& analysis) noexcept;

[[nodiscard]] std::optional<SongSection> find_section(
    const SectionMap& map,
    SectionKind kind) noexcept;

[[nodiscard]] TimelineSelection section_selection(
    const SectionMap& map,
    SectionKind kind) noexcept;

[[nodiscard]] TimelineSelection snap_selection_to_section(
    const SectionMap& map,
    const TimelineSelection& selection) noexcept;

[[nodiscard]] std::filesystem::path ether_sections_sidecar_path(
    const std::filesystem::path& audio_path);

bool save_section_map(
    const SectionMap& map,
    const std::filesystem::path& path) noexcept;

[[nodiscard]] std::optional<SectionMap> load_section_map(
    const std::filesystem::path& path) noexcept;

[[nodiscard]] std::optional<SectionMap> load_section_map_for_audio(
    const std::filesystem::path& audio_path) noexcept;

} // namespace etherbeat
