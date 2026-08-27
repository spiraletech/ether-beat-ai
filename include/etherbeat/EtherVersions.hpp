#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace etherbeat {

struct VersionRecord {
    std::string schema = "etherbeat.versions.v1";
    std::string version_id;
    std::string root_id;
    std::string parent_id;
    std::filesystem::path audio_path;
    std::filesystem::path parent_audio_path;
    std::string operation;
    std::string instruction;
    long long created_unix_ms = 0;
};

struct VersionLineage {
    VersionRecord current;
    std::optional<VersionRecord> parent;
    std::vector<VersionRecord> children;
    std::string promoted_version_id;

    bool current_is_promoted() const noexcept {
        return !current.version_id.empty() && current.version_id == promoted_version_id;
    }
};

class EtherVersions {
public:
    explicit EtherVersions(std::filesystem::path library_root);

    VersionRecord ensure_root(const std::filesystem::path& audio_path);

    VersionRecord register_child(
        const std::filesystem::path& parent_audio_path,
        const std::filesystem::path& child_audio_path,
        std::string operation,
        std::string instruction);

    std::optional<VersionRecord> load_for_audio(const std::filesystem::path& audio_path) const;
    VersionLineage lineage_for_audio(const std::filesystem::path& audio_path) const;

    void promote(const std::filesystem::path& audio_path);
    std::filesystem::path promoted_audio_for(const std::filesystem::path& audio_path) const;
    std::filesystem::path parent_audio_for(const std::filesystem::path& audio_path) const;

    static std::filesystem::path sidecar_path(const std::filesystem::path& audio_path);

private:
    std::filesystem::path library_root_;

    std::filesystem::path heads_root() const;
    std::filesystem::path head_path(const std::string& root_id) const;
    std::string read_head(const std::string& root_id) const;
    void write_head(const std::string& root_id, const std::string& version_id) const;
    std::vector<VersionRecord> scan_records() const;
};

} // namespace etherbeat
