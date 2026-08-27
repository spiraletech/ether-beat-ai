#include "etherbeat/EtherAssemble.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace {

etherbeat::PcmAudio make_pcm(double seconds, float amplitude, float frequency = 220.0f, std::uint32_t rate = 48'000) {
    etherbeat::PcmAudio audio;
    audio.sample_rate = rate;
    audio.channels = 2;
    const std::size_t frames = static_cast<std::size_t>(seconds * audio.sample_rate);
    audio.samples.resize(frames * audio.channels);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float phase = static_cast<float>(frame) / static_cast<float>(audio.sample_rate);
        const float value = amplitude * std::sin(6.28318530718f * frequency * phase);
        audio.samples[frame * 2u] = value;
        audio.samples[frame * 2u + 1u] = value;
    }
    return audio;
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "etherbeat-assemble-smoke";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    etherbeat::ArrangementPlan plan;
    plan.source_audio = root / "source.wav";
    plan.revision = 7;
    plan.slots = {
        {.slot_id="intro-1", .kind=etherbeat::SectionKind::Intro, .label="INTRO", .origin=etherbeat::ArrangementOrigin::Source, .source_start_seconds=0.0, .source_end_seconds=1.0},
        {.slot_id="hook-1", .kind=etherbeat::SectionKind::Hook, .label="HOOK", .origin=etherbeat::ArrangementOrigin::Source, .source_start_seconds=1.0, .source_end_seconds=2.0},
        {.slot_id="hook-2", .kind=etherbeat::SectionKind::Hook, .label="HOOK COPY", .origin=etherbeat::ArrangementOrigin::Duplicate, .origin_slot_id="hook-1", .source_start_seconds=1.0, .source_end_seconds=2.0},
        {.slot_id="bridge-alt", .kind=etherbeat::SectionKind::Bridge, .label="BRIDGE ALT", .origin=etherbeat::ArrangementOrigin::Placeholder, .instruction="generate alternate bridge"}
    };

    const auto source = make_pcm(2.5, 0.35f);
    int decode_calls = 0;
    int placeholder_calls = 0;

    etherbeat::EtherAssemble assembler;
    const auto output = root / "assembled.wav";
    const auto result = assembler.render(
        plan,
        output,
        [&](const fs::path& path) {
            assert(path == plan.source_audio);
            ++decode_calls;
            return source;
        },
        [&](const etherbeat::ArrangementSlot& slot, double expected_duration) -> std::optional<etherbeat::PcmAudio> {
            assert(slot.slot_id == "bridge-alt");
            assert(expected_duration > 0.9 && expected_duration < 1.1);
            ++placeholder_calls;
            return make_pcm(expected_duration, 0.20f, 330.0f, 44'100);
        },
        {.crossfade_seconds=0.010, .require_all_placeholders=true});

    assert(result.success());
    assert(fs::exists(result.audio_path));
    assert(fs::exists(result.manifest_path));
    assert(result.sample_rate == 48'000u);
    assert(result.channels == 2u);
    assert(result.slots.size() == 4u);
    assert(decode_calls == 1);
    assert(placeholder_calls == 1);
    assert(result.adaptive_seams);
    assert(!result.slots[0].generated);
    assert(!result.slots[0].seam_analyzed);
    assert(!result.slots[1].generated);
    assert(!result.slots[2].generated);
    assert(result.slots[3].generated);
    assert(result.slots[1].seam_analyzed);
    assert(result.slots[2].seam_analyzed);
    assert(result.slots[3].seam_analyzed);
    assert(result.slots[1].applied_crossfade_seconds >= 0.0049);
    assert(result.slots[3].applied_crossfade_seconds >= result.slots[1].applied_crossfade_seconds);
    assert(result.max_seam_score >= result.average_seam_score);
    assert(result.duration_seconds > 3.65 && result.duration_seconds < 4.0);
    assert(result.slots[1].output_start_seconds < 1.0);
    assert(std::abs(result.slots[3].output_end_seconds - result.duration_seconds) < 0.00001);

    std::ifstream wav(result.audio_path, std::ios::binary);
    char riff[4]{};
    wav.read(riff, 4);
    assert(std::string(riff, 4) == "RIFF");

    std::ifstream manifest(result.manifest_path);
    const std::string text(std::istreambuf_iterator<char>{manifest}, std::istreambuf_iterator<char>{});
    assert(text.find("etherbeat.assemble.v2") != std::string::npos);
    assert(text.find("\"arrangement_revision\": 7") != std::string::npos);
    assert(text.find("BRIDGE ALT") != std::string::npos);
    assert(text.find("\"generated\": true") != std::string::npos);
    assert(text.find("\"adaptive_seams\": true") != std::string::npos);
    assert(text.find("\"seam_score\"") != std::string::npos);
    assert(text.find("\"applied_crossfade_seconds\"") != std::string::npos);

    bool unresolved_rejected = false;
    try {
        static_cast<void>(assembler.render(plan, root / "should-fail.wav", [&](const fs::path&) { return source; }));
    } catch (...) {
        unresolved_rejected = true;
    }
    assert(unresolved_rejected);

    // Placeholder-only plans are also legal: the first generated fragment establishes output format.
    etherbeat::ArrangementPlan placeholder_only;
    placeholder_only.source_audio = root / "unused.wav";
    placeholder_only.slots = {{.slot_id="alt", .kind=etherbeat::SectionKind::Hook, .label="HOOK ALT", .origin=etherbeat::ArrangementOrigin::Placeholder, .instruction="hook"}};
    const auto generated_only = assembler.render(
        placeholder_only,
        root / "generated-only.wav",
        [&](const fs::path&) -> etherbeat::PcmAudio { assert(false); return {}; },
        [&](const etherbeat::ArrangementSlot&, double seconds) -> std::optional<etherbeat::PcmAudio> {
            return make_pcm(seconds, 0.15f, 440.0f, 44'100);
        });
    assert(generated_only.sample_rate == 44'100u);
    assert(generated_only.success());
    assert(generated_only.max_seam_score == 0.0);

    // Fixed-crossfade fallback remains available for deterministic comparisons.
    auto fixed_plan = plan;
    fixed_plan.slots.resize(2);
    const auto fixed = assembler.render(
        fixed_plan,
        root / "fixed.wav",
        [&](const fs::path&) { return source; },
        {},
        {.crossfade_seconds=0.012, .require_all_placeholders=true, .adaptive_seams=false});
    assert(!fixed.adaptive_seams);
    assert(std::abs(fixed.slots[1].applied_crossfade_seconds - 0.012) < 0.0001);

    fs::remove_all(root, ec);
    return 0;
}
