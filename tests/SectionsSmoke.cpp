#include "etherbeat/EtherSections.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    etherbeat::AudioAnalysis analysis;
    analysis.ready = true;
    analysis.duration_seconds = 100.0;
    analysis.sample_rate = 48000;
    analysis.channels = 2;
    analysis.timeline_envelope.fill(0.70f);

    // Put measurable valleys near the default structural neighborhoods so the
    // detector has real transition candidates instead of pure ratio slicing.
    analysis.timeline_envelope[15] = 0.05f;
    analysis.timeline_envelope[46] = 0.06f;
    analysis.timeline_envelope[79] = 0.04f;
    analysis.timeline_envelope[107] = 0.07f;

    const fs::path audio = fs::temp_directory_path() / "etherbeat-sections-smoke.wav";
    const auto map = etherbeat::infer_sections(audio, analysis);
    if (map.sections.size() != 5) {
        std::cerr << "expected five structural sections\n";
        return 1;
    }
    if (std::abs(map.sections.front().start_seconds) > 1e-9 ||
        std::abs(map.sections.back().end_seconds - 100.0) > 1e-6) {
        std::cerr << "sections must cover the complete song\n";
        return 2;
    }
    for (std::size_t i = 0; i + 1 < map.sections.size(); ++i) {
        if (!(map.sections[i].end_seconds > map.sections[i].start_seconds) ||
            std::abs(map.sections[i].end_seconds - map.sections[i + 1].start_seconds) > 1e-6) {
            std::cerr << "section ordering/continuity failed\n";
            return 3;
        }
    }

    const auto hook = etherbeat::find_section(map, etherbeat::SectionKind::Hook);
    if (!hook || hook->label != "HOOK") {
        std::cerr << "hook lookup failed\n";
        return 4;
    }

    const auto hookSelection = etherbeat::section_selection(map, etherbeat::SectionKind::Hook);
    if (!hookSelection.valid() ||
        std::abs(hookSelection.start_seconds - hook->start_seconds) > 1e-6 ||
        std::abs(hookSelection.end_seconds - hook->end_seconds) > 1e-6) {
        std::cerr << "section selection failed\n";
        return 5;
    }

    const auto loose = etherbeat::make_timeline_selection(100.0, 0.44, 0.53);
    const auto snapped = etherbeat::snap_selection_to_section(map, loose);
    if (!snapped.valid() ||
        std::abs(snapped.start_seconds - hook->start_seconds) > 1e-6 ||
        std::abs(snapped.end_seconds - hook->end_seconds) > 1e-6) {
        std::cerr << "timeline-to-section snap failed\n";
        return 6;
    }

    const auto sidecar = fs::temp_directory_path() / "etherbeat-sections-smoke.ethersections.json";
    if (!etherbeat::save_section_map(map, sidecar)) {
        std::cerr << "section map save failed\n";
        return 7;
    }
    const auto loaded = etherbeat::load_section_map(sidecar);
    if (!loaded || loaded->sections.size() != 5 ||
        std::abs(loaded->sections[2].start_seconds - map.sections[2].start_seconds) > 1e-5 ||
        std::abs(loaded->sections[4].end_seconds - 100.0) > 1e-5) {
        std::cerr << "section map round trip failed\n";
        return 8;
    }

    std::error_code ec;
    fs::remove(sidecar, ec);
    return 0;
}
