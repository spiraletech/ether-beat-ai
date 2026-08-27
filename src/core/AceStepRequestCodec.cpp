#include "etherbeat/AceStepRequestCodec.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace etherbeat {
namespace {

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char c : value) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                out << "\\u00" << hex[(c >> 4) & 0x0f] << hex[c & 0x0f];
            } else {
                out << static_cast<char>(c);
            }
            break;
        }
    }
    return out.str();
}

std::string path_utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void reject_unhonored_precision_controls(const GenerationRequest& request) {
    if (request.control.locks != 0) {
        throw std::invalid_argument("ACE-Step control adapter does not advertise component-lock preservation");
    }
    if (!request.control.drum_reference.empty()) {
        throw std::invalid_argument("ACE-Step control adapter does not advertise isolated drum conditioning");
    }
    if (!request.control.melody_reference.empty()) {
        throw std::invalid_argument("ACE-Step control adapter does not advertise isolated melody conditioning");
    }
    if (!request.control.chord_progression.empty()) {
        throw std::invalid_argument("ACE-Step control adapter does not advertise symbolic chord conditioning");
    }
}

const char* task_type_for(GenerationMode mode) {
    switch (mode) {
    case GenerationMode::TextToInstrumental: return "text2music";
    case GenerationMode::Variation: return "cover";
    case GenerationMode::AudioToAudio: return "cover";
    case GenerationMode::ReplaceSection: return "repaint";
    case GenerationMode::Extend:
        throw std::invalid_argument("ACE-Step Extend is not enabled in EtherBeat until continuation semantics are separately validated");
    }
    throw std::invalid_argument("Unknown EtherBeat generation mode");
}

bool uses_source_audio(GenerationMode mode) {
    return mode == GenerationMode::Variation ||
        mode == GenerationMode::AudioToAudio ||
        mode == GenerationMode::ReplaceSection;
}

} // namespace

ProviderCapabilities ace_step_provider_capabilities() noexcept {
    return capability(ProviderCapability::TextToInstrumental)
        | ProviderCapability::Variation
        | ProviderCapability::AudioToAudio
        | ProviderCapability::ReferenceAudio
        | ProviderCapability::ReplaceSection
        | ProviderCapability::DraftRole
        | ProviderCapability::QualityRole
        | ProviderCapability::ControlRole
        | ProviderCapability::LocalRuntime
        | ProviderCapability::TemporalControl;
}

AceStepRequestPayload build_ace_step_request_payload(const GenerationRequest& request) {
    if (request.prompt.empty()) {
        throw std::invalid_argument("ACE-Step request codec requires a prompt/instruction");
    }

    reject_unhonored_precision_controls(request);

    if (uses_source_audio(request.mode) && request.reference_audio.empty()) {
        throw std::invalid_argument("ACE-Step control jobs require source/reference audio");
    }

    if (request.mode == GenerationMode::ReplaceSection) {
        if (request.control.edit_start_seconds < 0.0 || request.control.edit_end_seconds <= request.control.edit_start_seconds) {
            throw std::invalid_argument("ACE-Step repaint requires a valid start/end edit window");
        }
    }

    const std::string task_type = task_type_for(request.mode);
    std::ostringstream body;
    body << "{"
         << "\"prompt\":\"" << json_escape(request.prompt) << "\","
         << "\"lyrics\":\"[Instrumental]\","
         << "\"thinking\":false,"
         << "\"instrumental\":true,"
         << "\"batch_size\":1,"
         << "\"audio_format\":\"wav\","
         << "\"audio_duration\":" << std::fixed << std::setprecision(3)
         << std::clamp(request.duration_seconds, 10.0, 600.0) << ","
         << "\"use_random_seed\":" << (request.seed == 0 ? "true" : "false") << ","
         << "\"seed\":" << (request.seed == 0 ? -1LL : static_cast<long long>(request.seed)) << ","
         << "\"task_type\":\"" << task_type << "\"";

    if (request.bpm >= 30.0 && request.bpm <= 300.0) {
        body << ",\"bpm\":" << static_cast<int>(request.bpm + 0.5);
    }
    if (!request.key.empty()) {
        body << ",\"key_scale\":\"" << json_escape(request.key) << "\"";
    }

    if (uses_source_audio(request.mode)) {
        body << ",\"src_audio_path\":\""
             << json_escape(path_utf8(std::filesystem::absolute(request.reference_audio))) << "\""
             << ",\"instruction\":\"" << json_escape(request.prompt) << "\"";
    }

    if (request.mode == GenerationMode::Variation || request.mode == GenerationMode::AudioToAudio) {
        body << ",\"audio_cover_strength\":" << std::setprecision(3)
             << std::clamp(request.control.reference_strength, 0.0, 1.0);
    }

    if (request.mode == GenerationMode::ReplaceSection) {
        body << ",\"repainting_start\":" << std::setprecision(3)
             << request.control.edit_start_seconds
             << ",\"repainting_end\":" << request.control.edit_end_seconds;
    }

    body << "}";
    return AceStepRequestPayload{task_type, body.str()};
}

} // namespace etherbeat
