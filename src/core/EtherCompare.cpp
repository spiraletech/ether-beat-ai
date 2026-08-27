#include "etherbeat/EtherCompare.hpp"

#include <algorithm>
#include <cmath>

namespace etherbeat {
namespace {

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float abs_delta(float a, float b) {
    return std::fabs(b - a);
}

} // namespace

EtherCompareResult compare_ether_dna(const EtherDNA& a, const EtherDNA& b) {
    EtherCompareResult result;
    result.a_audio = a.source_audio;
    result.b_audio = b.source_audio;
    result.a = a;
    result.b = b;

    result.delta.energy = b.energy - a.energy;
    result.delta.bass = b.bass - a.bass;
    result.delta.mid = b.mid - a.mid;
    result.delta.treble = b.treble - a.treble;
    result.delta.beat_peak = b.beat_peak - a.beat_peak;
    result.delta.low_end_weight = b.low_end_weight - a.low_end_weight;
    result.delta.brightness = b.brightness - a.brightness;
    result.delta.darkness = b.darkness - a.darkness;
    result.delta.rhythmic_activity = b.rhythmic_activity - a.rhythmic_activity;
    result.delta.spectral_center = b.spectral_center - a.spectral_center;

    double spectrum_squared = 0.0;
    for (std::size_t i = 0; i < a.spectrum.size(); ++i) {
        const double d = static_cast<double>(b.spectrum[i] - a.spectrum[i]);
        spectrum_squared += d * d;
    }
    result.delta.spectrum_rmse = static_cast<float>(
        std::sqrt(spectrum_squared / static_cast<double>(a.spectrum.size())));

    // Weighted measurable similarity. No semantic/taste claims are made here;
    // this is strictly how much the analyzed audio identity was preserved.
    const float distance =
        0.15f * abs_delta(a.energy, b.energy) +
        0.14f * abs_delta(a.low_end_weight, b.low_end_weight) +
        0.10f * abs_delta(a.mid, b.mid) +
        0.10f * abs_delta(a.brightness, b.brightness) +
        0.08f * abs_delta(a.darkness, b.darkness) +
        0.14f * abs_delta(a.rhythmic_activity, b.rhythmic_activity) +
        0.09f * abs_delta(a.spectral_center, b.spectral_center) +
        0.20f * clamp01(result.delta.spectrum_rmse);

    result.similarity = clamp01(1.0f - distance);
    return result;
}

std::optional<EtherCompareResult> compare_audio_dna(
    const std::filesystem::path& a_audio,
    const std::filesystem::path& b_audio) noexcept {
    try {
        const auto a = load_ether_dna_for_audio(a_audio);
        const auto b = load_ether_dna_for_audio(b_audio);
        if (!a || !b) return std::nullopt;
        return compare_ether_dna(*a, *b);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace etherbeat
