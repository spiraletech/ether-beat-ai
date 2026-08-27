#pragma once

#include <cstddef>

namespace etherbeat {

struct PcmAudio;

struct SeamOptions {
    double analysis_window_seconds{0.030};
    double min_crossfade_seconds{0.005};
    double max_crossfade_seconds{0.100};
    double severe_score{0.82};
};

struct SeamReport {
    bool ready{false};

    // 0.0 = continuous / low-risk boundary, 1.0 = highly discontinuous.
    double seam_score{0.0};

    double rms_jump{0.0};
    double spectral_jump{0.0};
    double dc_jump{0.0};
    double transient_collision{0.0};
    double sample_jump{0.0};

    double recommended_crossfade_seconds{0.0};
    bool severe{false};
};

[[nodiscard]] SeamReport analyze_seam(
    const PcmAudio& left,
    const PcmAudio& right,
    SeamOptions options = {});

} // namespace etherbeat
