#pragma once

namespace etherbeat {

struct TimelineSelection {
    double start_seconds{0.0};
    double end_seconds{0.0};
    double duration_seconds{0.0};

    [[nodiscard]] double length_seconds() const noexcept {
        return end_seconds - start_seconds;
    }

    [[nodiscard]] bool valid() const noexcept {
        return duration_seconds > 0.0 && start_seconds >= 0.0 &&
               end_seconds > start_seconds && end_seconds <= duration_seconds;
    }
};

[[nodiscard]] TimelineSelection make_timeline_selection(
    double duration_seconds,
    double normalized_a,
    double normalized_b,
    double minimum_length_seconds = 0.25) noexcept;

[[nodiscard]] double timeline_normalized_position(
    double duration_seconds,
    double seconds) noexcept;

} // namespace etherbeat
