#include "etherbeat/EtherTimeline.hpp"

#include <algorithm>
#include <cmath>

namespace etherbeat {
namespace {

double finite_or(double value, double fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

} // namespace

TimelineSelection make_timeline_selection(
    double duration_seconds,
    double normalized_a,
    double normalized_b,
    double minimum_length_seconds) noexcept {

    TimelineSelection result;
    result.duration_seconds = std::max(0.0, finite_or(duration_seconds, 0.0));
    if (result.duration_seconds <= 0.0) return result;

    double a = std::clamp(finite_or(normalized_a, 0.0), 0.0, 1.0);
    double b = std::clamp(finite_or(normalized_b, a), 0.0, 1.0);
    if (b < a) std::swap(a, b);

    result.start_seconds = a * result.duration_seconds;
    result.end_seconds = b * result.duration_seconds;

    const double minimum = std::clamp(
        finite_or(minimum_length_seconds, 0.25),
        0.0,
        result.duration_seconds);

    if ((result.end_seconds - result.start_seconds) < minimum) {
        const double midpoint = (result.start_seconds + result.end_seconds) * 0.5;
        result.start_seconds = midpoint - minimum * 0.5;
        result.end_seconds = midpoint + minimum * 0.5;

        if (result.start_seconds < 0.0) {
            result.end_seconds -= result.start_seconds;
            result.start_seconds = 0.0;
        }
        if (result.end_seconds > result.duration_seconds) {
            const double overflow = result.end_seconds - result.duration_seconds;
            result.start_seconds -= overflow;
            result.end_seconds = result.duration_seconds;
        }
        result.start_seconds = std::max(0.0, result.start_seconds);
    }

    result.start_seconds = std::clamp(result.start_seconds, 0.0, result.duration_seconds);
    result.end_seconds = std::clamp(result.end_seconds, result.start_seconds, result.duration_seconds);
    return result;
}

double timeline_normalized_position(double duration_seconds, double seconds) noexcept {
    const double duration = finite_or(duration_seconds, 0.0);
    if (duration <= 0.0) return 0.0;
    return std::clamp(finite_or(seconds, 0.0) / duration, 0.0, 1.0);
}

} // namespace etherbeat
