#include "etherbeat/EtherArrangement.hpp"

#include <cassert>
#include <filesystem>
#include <string>

int main() {
    namespace fs = std::filesystem;
    using namespace etherbeat;

    SectionMap sections;
    sections.source_audio = fs::path("arrangement-source.wav");
    sections.duration_seconds = 100.0;
    sections.sections = {
        {SectionKind::Intro, "INTRO", 0.0, 10.0, 0.8f},
        {SectionKind::Verse, "VERSE", 10.0, 30.0, 0.8f},
        {SectionKind::Hook, "HOOK", 30.0, 50.0, 0.9f},
        {SectionKind::Bridge, "BRIDGE", 50.0, 75.0, 0.7f},
        {SectionKind::Outro, "OUTRO", 75.0, 100.0, 0.8f}
    };

    auto plan = make_arrangement_plan(sections);
    assert(plan.schema == "etherbeat.arrangement.v1");
    assert(plan.slots.size() == 5);
    assert(plan.revision == 0);
    assert(plan.slots[2].kind == SectionKind::Hook);
    assert(plan.slots[2].has_source_audio());

    const std::string originalHookId = plan.slots[2].slot_id;
    assert(duplicate_arrangement_slot(plan, 2));
    assert(plan.slots.size() == 6);
    assert(plan.revision == 1);
    assert(plan.slots[3].origin == ArrangementOrigin::Duplicate);
    assert(plan.slots[3].origin_slot_id == originalHookId);
    assert(plan.slots[3].source_start_seconds == 30.0);

    assert(move_arrangement_slot(plan, 3, 4));
    assert(plan.revision == 2);
    assert(plan.slots[4].origin == ArrangementOrigin::Duplicate);

    assert(insert_arrangement_placeholder(
        plan,
        2,
        SectionKind::Verse,
        "VERSE B",
        "generate a colder alternate verse"));
    assert(plan.revision == 3);
    assert(plan.slots[2].origin == ArrangementOrigin::Placeholder);
    assert(!plan.slots[2].has_source_audio());
    assert(plan.slots[2].instruction.find("colder") != std::string::npos);

    assert(erase_arrangement_slot(plan, 1));
    assert(plan.revision == 4);
    assert(plan.slots.size() == 6);

    const std::string blueprint = arrangement_blueprint(plan);
    assert(blueprint.find("Arrangement revision 4") != std::string::npos);
    assert(blueprint.find("HOOK") != std::string::npos);
    assert(blueprint.find("placeholder") != std::string::npos);
    assert(blueprint.find("duplicate") != std::string::npos);

    const fs::path temp = fs::temp_directory_path() / "etherbeat-arrangement-smoke.etherarrangement.json";
    assert(save_arrangement_plan(plan, temp));
    const auto loaded = load_arrangement_plan(temp);
    assert(loaded.has_value());
    assert(loaded->revision == plan.revision);
    assert(loaded->slots.size() == plan.slots.size());
    assert(loaded->slots[1].label == "VERSE B");
    assert(loaded->slots[1].origin == ArrangementOrigin::Placeholder);
    assert(loaded->slots[1].instruction == "generate a colder alternate verse");

    assert(ether_arrangement_sidecar_path(fs::path("song.wav")) == fs::path("song.wav.etherarrangement.json"));

    std::error_code ec;
    fs::remove(temp, ec);
    return 0;
}
