#pragma once

#include "etherbeat/GenerationTypes.hpp"

#include <string>
#include <vector>

namespace etherbeat {

struct CompositionSection {
    std::string name;
    int bars{0};
    double energy{0.5};
};

struct CompositionPlan {
    std::string source_prompt;
    std::string renderer_prompt;
    double bpm{0.0};
    std::string key;
    double duration_seconds{0.0};

    double energy{0.5};
    double drum_density{0.5};
    double bass_weight{0.5};
    double vocal_space{0.5};
    double texture_grit{0.5};
    double transient_density{0.5};

    std::vector<std::string> tags;
    std::vector<CompositionSection> sections;

    [[nodiscard]] std::string summary() const;
};

class EtherComposer {
public:
    [[nodiscard]] CompositionPlan compose(const GenerationRequest& request) const;

    // Produces the renderer-facing request while preserving seed, mode,
    // mutation amount and reference audio lineage from the original request.
    [[nodiscard]] GenerationRequest compile(
        const GenerationRequest& request,
        const CompositionPlan& plan) const;
};

} // namespace etherbeat
