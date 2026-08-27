#include "etherbeat/EtherSeam.hpp"

#include "etherbeat/EtherAssemble.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <numeric>
#include <vector>

namespace etherbeat {
namespace {

constexpr double kEpsilon = 1.0e-9;

double clamp01(double value) noexcept {
    return std::clamp(value, 0.0, 1.0);
}

std::vector<float> mono_boundary_window(const PcmAudio& audio, std::size_t frames, bool tail) {
    frames = std::min(frames, audio.frame_count());
    std::vector<float> mono(frames, 0.0f);
    if (frames == 0 || audio.channels == 0) return mono;

    const std::size_t first_frame = tail ? audio.frame_count() - frames : 0u;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t base = (first_frame + frame) * audio.channels;
        double sum = 0.0;
        for (std::size_t ch = 0; ch < audio.channels; ++ch) sum += audio.samples[base + ch];
        mono[frame] = static_cast<float>(sum / static_cast<double>(audio.channels));
    }
    return mono;
}

double mean_value(const std::vector<float>& values) noexcept {
    if (values.empty()) return 0.0;
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / static_cast<double>(values.size());
}

double rms_value(const std::vector<float>& values) noexcept {
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (const float value : values) sum += static_cast<double>(value) * value;
    return std::sqrt(sum / static_cast<double>(values.size()));
}

double transient_activity(const std::vector<float>& values) noexcept {
    if (values.size() < 2) return 0.0;
    double derivative_sum = 0.0;
    double derivative_peak = 0.0;
    for (std::size_t i = 1; i < values.size(); ++i) {
        const double d = std::abs(static_cast<double>(values[i]) - values[i - 1]);
        derivative_sum += d;
        derivative_peak = std::max(derivative_peak, d);
    }
    const double mean_derivative = derivative_sum / static_cast<double>(values.size() - 1u);
    const double rms = rms_value(values);
    return clamp01((mean_derivative * 5.0 + derivative_peak * 0.65) / (rms + 0.08));
}

double goertzel_magnitude(const std::vector<float>& samples, double sample_rate, double frequency) noexcept {
    if (samples.empty() || sample_rate <= 0.0 || frequency <= 0.0) return 0.0;
    frequency = std::min(frequency, sample_rate * 0.45);
    const double omega = 2.0 * std::numbers::pi * frequency / sample_rate;
    const double coeff = 2.0 * std::cos(omega);
    double q0 = 0.0, q1 = 0.0, q2 = 0.0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double hann = samples.size() <= 1
            ? 1.0
            : 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                   static_cast<double>(samples.size() - 1u));
        q0 = static_cast<double>(samples[i]) * hann + coeff * q1 - q2;
        q2 = q1;
        q1 = q0;
    }
    const double power = std::max(0.0, q1 * q1 + q2 * q2 - coeff * q1 * q2);
    return std::sqrt(power) / static_cast<double>(samples.size());
}

std::array<double, 8> spectral_signature(const std::vector<float>& values, std::uint32_t sample_rate) noexcept {
    constexpr std::array<double, 8> frequencies{80.0, 180.0, 400.0, 900.0, 1800.0, 3500.0, 7000.0, 12000.0};
    std::array<double, 8> signature{};
    double sum = 0.0;
    for (std::size_t i = 0; i < signature.size(); ++i) {
        signature[i] = goertzel_magnitude(values, static_cast<double>(sample_rate), frequencies[i]);
        sum += signature[i];
    }
    if (sum > kEpsilon) {
        for (double& value : signature) value /= sum;
    }
    return signature;
}

double spectral_distance(const std::array<double, 8>& left, const std::array<double, 8>& right) noexcept {
    double distance = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) distance += std::abs(left[i] - right[i]);
    return clamp01(distance * 0.5);
}

} // namespace

SeamReport analyze_seam(const PcmAudio& left, const PcmAudio& right, SeamOptions options) {
    SeamReport report;
    if (!left.valid() || !right.valid()) return report;
    if (left.sample_rate != right.sample_rate || left.channels != right.channels) return report;

    options.analysis_window_seconds = std::clamp(options.analysis_window_seconds, 0.005, 0.100);
    options.min_crossfade_seconds = std::clamp(options.min_crossfade_seconds, 0.0, 0.250);
    options.max_crossfade_seconds = std::clamp(options.max_crossfade_seconds,
                                                options.min_crossfade_seconds,
                                                0.250);
    options.severe_score = clamp01(options.severe_score);

    const std::size_t requested_frames = std::max<std::size_t>(
        32u,
        static_cast<std::size_t>(std::llround(options.analysis_window_seconds * left.sample_rate)));
    const std::size_t frames = std::min({requested_frames, left.frame_count(), right.frame_count()});
    if (frames < 8u) return report;

    const auto left_window = mono_boundary_window(left, frames, true);
    const auto right_window = mono_boundary_window(right, frames, false);

    const double left_rms = rms_value(left_window);
    const double right_rms = rms_value(right_window);
    report.rms_jump = clamp01(std::abs(left_rms - right_rms) /
                              (std::max(left_rms, right_rms) + 0.05));

    const double left_dc = mean_value(left_window);
    const double right_dc = mean_value(right_window);
    report.dc_jump = clamp01(std::abs(left_dc - right_dc) / 0.50);

    report.spectral_jump = spectral_distance(
        spectral_signature(left_window, left.sample_rate),
        spectral_signature(right_window, right.sample_rate));

    const double left_transient = transient_activity(left_window);
    const double right_transient = transient_activity(right_window);
    report.transient_collision = clamp01(std::min(left_transient, right_transient) * 0.75 +
                                         std::abs(left_transient - right_transient) * 0.20);

    const double boundary_delta = std::abs(
        static_cast<double>(left_window.back()) - static_cast<double>(right_window.front()));
    report.sample_jump = clamp01(boundary_delta / 1.25);

    report.seam_score = clamp01(
        report.rms_jump * 0.22 +
        report.spectral_jump * 0.30 +
        report.dc_jump * 0.14 +
        report.transient_collision * 0.20 +
        report.sample_jump * 0.14);

    const double shaped = std::pow(report.seam_score, 0.72);
    report.recommended_crossfade_seconds =
        options.min_crossfade_seconds +
        (options.max_crossfade_seconds - options.min_crossfade_seconds) * shaped;
    report.recommended_crossfade_seconds = std::clamp(
        report.recommended_crossfade_seconds,
        options.min_crossfade_seconds,
        options.max_crossfade_seconds);

    report.severe = report.seam_score >= options.severe_score;
    report.ready = true;
    return report;
}

} // namespace etherbeat
