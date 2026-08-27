#include "etherbeat/EtherTimeline.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double a, double b, double eps = 1e-9) {
    return std::abs(a - b) <= eps;
}

} // namespace

int main() {
    try {
        using etherbeat::make_timeline_selection;
        using etherbeat::timeline_normalized_position;

        const auto normal = make_timeline_selection(100.0, 0.20, 0.55);
        require(normal.valid(), "normal timeline selection invalid");
        require(near(normal.start_seconds, 20.0), "normal selection start mismatch");
        require(near(normal.end_seconds, 55.0), "normal selection end mismatch");

        const auto reversed = make_timeline_selection(100.0, 0.80, 0.30);
        require(near(reversed.start_seconds, 30.0), "reversed drag start mismatch");
        require(near(reversed.end_seconds, 80.0), "reversed drag end mismatch");

        const auto clamped = make_timeline_selection(10.0, -2.0, 4.0);
        require(near(clamped.start_seconds, 0.0), "clamped start mismatch");
        require(near(clamped.end_seconds, 10.0), "clamped end mismatch");

        const auto minimum = make_timeline_selection(10.0, 0.50, 0.50, 0.50);
        require(minimum.valid(), "minimum-width selection invalid");
        require(minimum.length_seconds() >= 0.50 - 1e-9,
                "minimum-width selection was not expanded");

        const auto edge = make_timeline_selection(1.0, 0.99, 0.99, 0.25);
        require(edge.valid(), "edge selection invalid");
        require(near(edge.end_seconds, 1.0), "edge selection should clamp to duration");
        require(edge.length_seconds() >= 0.25 - 1e-9,
                "edge selection lost minimum width");

        require(near(timeline_normalized_position(20.0, 5.0), 0.25),
                "normalized timeline position mismatch");
        require(near(timeline_normalized_position(20.0, 100.0), 1.0),
                "normalized timeline position did not clamp");

        std::cout << "EtherTimeline smoke passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "EtherTimeline smoke failed: " << e.what() << "\n";
        return 1;
    }
}
