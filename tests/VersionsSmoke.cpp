#include "etherbeat/EtherVersions.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void touch(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << "RIFFetherbeat";
}

} // namespace

int main() {
    try {
        const auto root = fs::temp_directory_path() / "etherbeat_versions_smoke";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);

        const auto original = root / "original.wav";
        const auto variation = root / "control" / "variation.wav";
        const auto repaint = root / "control" / "repaint.wav";
        touch(original);
        touch(variation);
        touch(repaint);

        etherbeat::EtherVersions versions(root);
        const auto v0 = versions.ensure_root(original);
        require(!v0.root_id.empty(), "root id missing");
        require(v0.parent_id.empty(), "root should not have a parent");
        require(versions.promoted_audio_for(original) == original,
                "root should be promoted by default");

        const auto v1 = versions.register_child(
            original, variation, "variation", "make it colder");
        require(v1.root_id == v0.root_id, "variation root id changed");
        require(v1.parent_id == v0.version_id, "variation parent mismatch");
        require(v1.parent_audio_path == original, "variation parent path mismatch");

        const auto v2 = versions.register_child(
            variation, repaint, "replace_section", "empty out the hook");
        require(v2.root_id == v0.root_id, "repaint root id changed");
        require(v2.parent_id == v1.version_id, "repaint parent mismatch");

        auto lineage = versions.lineage_for_audio(variation);
        require(lineage.parent.has_value(), "variation parent not found");
        require(lineage.parent->audio_path == original, "loaded parent path mismatch");
        require(lineage.children.size() == 1, "variation should have one child");
        require(lineage.children.front().audio_path == repaint, "child path mismatch");
        require(!lineage.current_is_promoted(), "variation should not initially be promoted");

        versions.promote(repaint);
        require(versions.promoted_audio_for(original) == repaint,
                "promoted repaint not resolved from root");
        auto repaint_lineage = versions.lineage_for_audio(repaint);
        require(repaint_lineage.current_is_promoted(), "repaint HEAD flag missing");

        const auto parent = versions.parent_audio_for(repaint);
        require(parent == variation, "rollback parent path mismatch");
        versions.promote(parent);
        require(versions.promoted_audio_for(repaint) == variation,
                "rollback promotion did not move HEAD to parent");

        require(fs::exists(etherbeat::EtherVersions::sidecar_path(original)),
                "root sidecar missing");
        require(fs::exists(etherbeat::EtherVersions::sidecar_path(variation)),
                "variation sidecar missing");
        require(fs::exists(etherbeat::EtherVersions::sidecar_path(repaint)),
                "repaint sidecar missing");

        fs::remove_all(root, ec);
        std::cout << "EtherVersions smoke passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "EtherVersions smoke failed: " << e.what() << "\n";
        return 1;
    }
}
