#pragma once

#include "etherbeat/EtherSections.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace etherbeat {

enum class ArrangementOrigin {
    Source,
    Duplicate,
    Placeholder
};

struct ArrangementSlot {
    std::string slot_id;
    SectionKind kind{SectionKind::Verse};
    std::string label;
    ArrangementOrigin origin{ArrangementOrigin::Source};
    std::string origin_slot_id;
    double source_start_seconds{-1.0};
    double source_end_seconds{-1.0};
    std::string instruction;

    [[nodiscard]] bool has_source_audio() const noexcept {
        return source_start_seconds >= 0.0 && source_end_seconds > source_start_seconds;
    }
};

struct ArrangementPlan {
    std::string schema{"etherbeat.arrangement.v1"};
    std::filesystem::path source_audio;
    std::uint64_t revision{0};
    std::vector<ArrangementSlot> slots;
};

[[nodiscard]] const char* arrangement_origin_name(ArrangementOrigin origin) noexcept;

[[nodiscard]] ArrangementPlan make_arrangement_plan(const SectionMap& sections);

bool duplicate_arrangement_slot(
    ArrangementPlan& plan,
    std::size_t index,
    bool insert_after = true);

bool move_arrangement_slot(
    ArrangementPlan& plan,
    std::size_t from_index,
    std::size_t to_index);

bool insert_arrangement_placeholder(
    ArrangementPlan& plan,
    std::size_t index,
    SectionKind kind,
    std::string label,
    std::string instruction = {});

bool erase_arrangement_slot(
    ArrangementPlan& plan,
    std::size_t index);

[[nodiscard]] std::string arrangement_blueprint(const ArrangementPlan& plan);

[[nodiscard]] std::filesystem::path ether_arrangement_sidecar_path(
    const std::filesystem::path& audio_path);

bool save_arrangement_plan(
    const ArrangementPlan& plan,
    const std::filesystem::path& path) noexcept;

[[nodiscard]] std::optional<ArrangementPlan> load_arrangement_plan(
    const std::filesystem::path& path) noexcept;

[[nodiscard]] std::optional<ArrangementPlan> load_arrangement_plan_for_audio(
    const std::filesystem::path& audio_path) noexcept;

} // namespace etherbeat
