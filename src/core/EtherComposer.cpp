#include "etherbeat/EtherComposer.hpp"
#include "etherbeat/EtherDNA.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace etherbeat {
namespace {

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool has(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

bool has_any(const std::string& text, std::initializer_list<std::string_view> needles) {
    for (const auto needle : needles) {
        if (has(text, needle)) return true;
    }
    return false;
}

void add_tag(CompositionPlan& plan, std::string tag) {
    if (std::find(plan.tags.begin(), plan.tags.end(), tag) == plan.tags.end()) {
        plan.tags.push_back(std::move(tag));
    }
}

double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

std::string intensity(double value) {
    if (value < 0.22) return "very low";
    if (value < 0.42) return "low";
    if (value < 0.62) return "medium";
    if (value < 0.82) return "high";
    return "very high";
}

std::string space_label(double value) {
    if (value < 0.25) return "tight";
    if (value < 0.5) return "moderate";
    if (value < 0.75) return "wide";
    return "enormous";
}

std::string texture_label(double value) {
    if (value < 0.2) return "pristine";
    if (value < 0.4) return "clean";
    if (value < 0.65) return "textured";
    if (value < 0.85) return "degraded";
    return "heavily degraded";
}

void build_sections(CompositionPlan& plan) {
    const double duration = std::max(10.0, plan.duration_seconds);
    plan.sections.clear();

    if (duration <= 24.0) {
        plan.sections = {
            {"intro", 4, clamp01(plan.energy * 0.62)},
            {"A", 8, plan.energy},
            {"variation", 4, clamp01(plan.energy * 1.06)},
            {"outro", 4, clamp01(plan.energy * 0.55)}
        };
    } else if (duration <= 50.0) {
        plan.sections = {
            {"intro", 4, clamp01(plan.energy * 0.58)},
            {"A", 8, plan.energy},
            {"hook", 8, clamp01(plan.energy * 1.12)},
            {"A2", 8, clamp01(plan.energy * 0.96)},
            {"outro", 4, clamp01(plan.energy * 0.52)}
        };
    } else {
        plan.sections = {
            {"intro", 8, clamp01(plan.energy * 0.55)},
            {"A", 16, plan.energy},
            {"hook", 8, clamp01(plan.energy * 1.14)},
            {"A2", 16, clamp01(plan.energy * 0.96)},
            {"bridge", 8, clamp01(plan.energy * 0.78)},
            {"final", 16, clamp01(plan.energy * 1.08)},
            {"outro", 8, clamp01(plan.energy * 0.5)}
        };
    }
}

std::string section_string(const CompositionPlan& plan) {
    std::ostringstream out;
    for (std::size_t i = 0; i < plan.sections.size(); ++i) {
        if (i) out << ", ";
        out << plan.sections[i].name << " " << plan.sections[i].bars << " bars";
    }
    return out.str();
}

} // namespace

CompositionPlan EtherComposer::compose(const GenerationRequest& request) const {
    CompositionPlan plan;
    plan.source_prompt = request.prompt;
    plan.bpm = request.bpm > 0.0 ? request.bpm : 72.0;
    plan.key = request.key.empty() ? "unspecified" : request.key;
    plan.duration_seconds = request.duration_seconds;

    const std::string prompt = lower_copy(request.prompt);

    // Neutral producer defaults. These are semantic controls, not model-specific knobs.
    plan.energy = 0.5;
    plan.drum_density = 0.48;
    plan.bass_weight = 0.52;
    plan.vocal_space = 0.5;
    plan.texture_grit = 0.42;
    plan.transient_density = 0.48;

    if (has_any(prompt, {"haunted", "haunting", "lonely", "melancholy", "sad", "dark"})) {
        plan.energy -= 0.12;
        plan.texture_grit += 0.10;
        add_tag(plan, "haunted");
    }
    if (has_any(prompt, {"aggressive", "violent", "hard", "explosive", "rage", "intense"})) {
        plan.energy += 0.25;
        plan.drum_density += 0.18;
        plan.transient_density += 0.22;
        add_tag(plan, "aggressive");
    }
    if (has_any(prompt, {"floating", "dreamy", "weightless", "ambient", "ethereal"})) {
        plan.energy -= 0.08;
        plan.vocal_space += 0.22;
        plan.transient_density -= 0.12;
        add_tag(plan, "floating");
    }
    if (has_any(prompt, {"negative space", "huge space", "enormous space", "empty room", "sparse", "minimal"})) {
        plan.drum_density -= 0.24;
        plan.transient_density -= 0.20;
        plan.vocal_space += 0.32;
        add_tag(plan, "negative-space");
    }
    if (has_any(prompt, {"busy drums", "dense drums", "breakbeat", "drum heavy", "drum-heavy"})) {
        plan.drum_density += 0.26;
        plan.transient_density += 0.24;
        add_tag(plan, "dense-drums");
    }
    if (has_any(prompt, {"submerged bass", "deep bass", "heavy bass", "bass heavy", "bass-heavy", "low end"})) {
        plan.bass_weight += 0.28;
        add_tag(plan, "bass-forward");
    }
    if (has_any(prompt, {"degraded", "dusty", "cheap headphones", "youtube rip", "tape", "cassette", "lo-fi", "lofi"})) {
        plan.texture_grit += 0.30;
        add_tag(plan, "degraded");
    }
    if (has_any(prompt, {"clean", "polished", "hi-fi", "hifi", "pristine", "glossy"})) {
        plan.texture_grit -= 0.25;
        add_tag(plan, "polished");
    }
    if (has_any(prompt, {"cloud rap", "cloud-rap", "2016 soundcloud", "soundcloud"})) {
        plan.drum_density -= 0.08;
        plan.vocal_space += 0.12;
        add_tag(plan, "cloud-rap");
    }
    if (has_any(prompt, {"soulful", "soul sample", "chopped sample", "sample loop", "sample-based"})) {
        plan.energy -= 0.03;
        plan.vocal_space += 0.08;
        add_tag(plan, "sample-led");
    }

    plan.energy = clamp01(plan.energy);
    plan.drum_density = clamp01(plan.drum_density);
    plan.bass_weight = clamp01(plan.bass_weight);
    plan.vocal_space = clamp01(plan.vocal_space);
    plan.texture_grit = clamp01(plan.texture_grit);
    plan.transient_density = clamp01(plan.transient_density);

    build_sections(plan);

    std::ostringstream renderer;
    renderer << request.prompt
             << ". Composition blueprint: "
             << std::fixed << std::setprecision(0) << plan.bpm << " BPM; "
             << "key " << plan.key << "; "
             << "energy " << intensity(plan.energy) << "; "
             << "drum density " << intensity(plan.drum_density) << "; "
             << "bass weight " << intensity(plan.bass_weight) << "; "
             << "transient density " << intensity(plan.transient_density) << "; "
             << "vocal space " << space_label(plan.vocal_space) << "; "
             << "texture " << texture_label(plan.texture_grit) << "; "
             << "arrangement " << section_string(plan) << ". "
             << "Preserve a coherent motif across sections; evolve arrangement without random genre switching.";

    if (!request.reference_audio.empty()) {
        if (const auto dna = load_ether_dna_for_audio(request.reference_audio)) {
            renderer << " Measured " << dna->conditioning_summary()
                     << ". Treat these measurements as reference identity targets, not as genre labels.";
            add_tag(plan, "etherdna-reference");
        }
    }

    if (std::find(plan.tags.begin(), plan.tags.end(), "cloud-rap") != plan.tags.end() &&
        !has_any(prompt, {"edm", "dance", "festival", "house", "techno"})) {
        renderer << " Avoid festival-style EDM buildups and drops; keep transitions understated.";
    }
    if (plan.vocal_space >= 0.72) {
        renderer << " Leave intentional midrange and rhythmic negative space for a future vocal.";
    }
    if (plan.drum_density <= 0.35) {
        renderer << " Do not fill every subdivision with percussion.";
    }

    plan.renderer_prompt = renderer.str();
    return plan;
}

GenerationRequest EtherComposer::compile(
    const GenerationRequest& request,
    const CompositionPlan& plan) const {

    GenerationRequest compiled = request;
    compiled.prompt = plan.renderer_prompt;
    if (plan.bpm > 0.0) compiled.bpm = plan.bpm;
    if (!plan.key.empty() && plan.key != "unspecified") compiled.key = plan.key;
    if (plan.duration_seconds > 0.0) compiled.duration_seconds = plan.duration_seconds;
    return compiled;
}

std::string CompositionPlan::summary() const {
    std::ostringstream out;
    out << std::fixed << std::setprecision(0)
        << bpm << " BPM // " << key
        << " // ENERGY " << intensity(energy)
        << " // DRUMS " << intensity(drum_density)
        << " // SPACE " << space_label(vocal_space)
        << " // TEXTURE " << texture_label(texture_grit);
    return out.str();
}

} // namespace etherbeat
