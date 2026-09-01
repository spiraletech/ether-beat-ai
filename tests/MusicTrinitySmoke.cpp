#include "etherbeat/MusicTrinityAdapter.hpp"

#include <cassert>
#include <string>

int main() {
    using namespace etherbeat;

    TrinityRequestV1 request;
    request.request_id = "req-001";
    request.project_id = "nightdrive_04";
    request.intent.prompt = "Make a colder, grimier beat with space to rap";
    request.intent.bpm = 86.0;
    request.intent.key = "F# minor";
    request.intent.reference_audio = "reference.wav";
    request.intent.drum_density = 0.31;
    request.intent.vocal_space = 0.88;
    request.intent.texture_grit = 0.79;
    request.intent.locks =
        ControlComponent::Harmony |
        ControlComponent::Arrangement |
        ControlComponent::Bass;
    request.intent.arrangement = {"intro", "verse", "hook", "verse", "outro"};

    MusicTrinityAdapter adapter;
    const auto compiled = adapter.compile(request);

    assert(compiled.request_id == request.request_id);
    assert(compiled.project_id == request.project_id);
    assert(compiled.generation_request.mode == GenerationMode::Variation);
    assert(compiled.generation_request.bpm == 86.0);
    assert(compiled.generation_request.key == "F# minor");
    assert(compiled.generation_request.reference_audio == request.intent.reference_audio);
    assert(has_control_component(compiled.generation_request.control.locks, ControlComponent::Harmony));
    assert(has_control_component(compiled.generation_request.control.locks, ControlComponent::Arrangement));
    assert(has_control_component(compiled.generation_request.control.locks, ControlComponent::Bass));
    assert(!has_control_component(compiled.generation_request.control.locks, ControlComponent::Drums));
    assert(compiled.generation_request.prompt.find("drum_density=0.31") != std::string::npos);
    assert(compiled.generation_request.prompt.find("vocal_space=0.88") != std::string::npos);
    assert(compiled.generation_request.prompt.find("arrangement=intro/verse/hook/verse/outro") != std::string::npos);

    return 0;
}
