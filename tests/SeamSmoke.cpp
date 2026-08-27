#include "etherbeat/EtherAssemble.hpp"
#include "etherbeat/EtherSeam.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>

namespace {

etherbeat::PcmAudio constant_pcm(float value, double seconds = 0.08) {
    etherbeat::PcmAudio audio;
    audio.sample_rate = 48'000;
    audio.channels = 2;
    const std::size_t frames = static_cast<std::size_t>(seconds * audio.sample_rate);
    audio.samples.resize(frames * audio.channels, value);
    return audio;
}

etherbeat::PcmAudio sine_pcm(float amplitude, float frequency, double phase_offset = 0.0, double seconds = 0.08) {
    etherbeat::PcmAudio audio;
    audio.sample_rate = 48'000;
    audio.channels = 2;
    const std::size_t frames = static_cast<std::size_t>(seconds * audio.sample_rate);
    audio.samples.resize(frames * audio.channels);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double t = static_cast<double>(frame) / audio.sample_rate;
        const float sample = amplitude * static_cast<float>(std::sin(6.283185307179586 * frequency * t + phase_offset));
        audio.samples[frame * 2u] = sample;
        audio.samples[frame * 2u + 1u] = sample;
    }
    return audio;
}

} // namespace

int main() {
    const auto clean_left = constant_pcm(0.12f);
    const auto clean_right = constant_pcm(0.12f);
    const auto clean = etherbeat::analyze_seam(clean_left, clean_right);
    assert(clean.ready);
    assert(clean.seam_score < 0.08);
    assert(clean.recommended_crossfade_seconds >= 0.005);
    assert(clean.recommended_crossfade_seconds < 0.030);
    assert(!clean.severe);

    const auto tonal_left = sine_pcm(0.72f, 110.0f);
    auto harsh_right = sine_pcm(0.12f, 8'000.0f, 3.141592653589793);
    for (std::size_t i = 0; i < harsh_right.samples.size(); ++i) {
        harsh_right.samples[i] += (i % 4u < 2u) ? 0.45f : -0.45f;
    }
    const auto harsh = etherbeat::analyze_seam(tonal_left, harsh_right);
    assert(harsh.ready);
    assert(harsh.seam_score > clean.seam_score + 0.20);
    assert(harsh.spectral_jump > clean.spectral_jump);
    assert(harsh.rms_jump > clean.rms_jump);
    assert(harsh.recommended_crossfade_seconds > clean.recommended_crossfade_seconds);
    assert(harsh.recommended_crossfade_seconds <= 0.100001);

    const auto dc_left = constant_pcm(0.70f);
    const auto dc_right = constant_pcm(-0.70f);
    const auto dc = etherbeat::analyze_seam(dc_left, dc_right, {.severe_score = 0.30});
    assert(dc.ready);
    assert(dc.dc_jump > 0.99);
    assert(dc.sample_jump > 0.99);
    assert(dc.severe);

    auto mismatch = clean_right;
    mismatch.sample_rate = 44'100;
    assert(!etherbeat::analyze_seam(clean_left, mismatch).ready);

    return 0;
}
