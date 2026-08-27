#include "etherbeat/EtherDNA.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>

namespace etherbeat {
namespace {

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

std::string path_utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string escape_json(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const unsigned char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c >= 0x20) out.push_back(static_cast<char>(c));
            break;
        }
    }
    return out;
}

std::optional<double> number_after(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const auto keyPos = text.find(needle);
    if (keyPos == std::string::npos) return std::nullopt;
    const auto colon = text.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return std::nullopt;
    const char* begin = text.c_str() + colon + 1;
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<std::string> string_after(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const auto keyPos = text.find(needle);
    if (keyPos == std::string::npos) return std::nullopt;
    const auto colon = text.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return std::nullopt;
    const auto quote = text.find('"', colon + 1);
    if (quote == std::string::npos) return std::nullopt;

    std::string out;
    bool escaped = false;
    for (std::size_t i = quote + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (escaped) {
            switch (c) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: out.push_back(c); break;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            return out;
        } else {
            out.push_back(c);
        }
    }
    return std::nullopt;
}

bool parse_spectrum(const std::string& text, std::array<float, 32>& spectrum) {
    const auto keyPos = text.find("\"spectrum\"");
    if (keyPos == std::string::npos) return false;
    const auto open = text.find('[', keyPos);
    const auto close = text.find(']', open == std::string::npos ? keyPos : open);
    if (open == std::string::npos || close == std::string::npos) return false;

    const char* cursor = text.c_str() + open + 1;
    const char* limit = text.c_str() + close;
    for (std::size_t i = 0; i < spectrum.size(); ++i) {
        while (cursor < limit && (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t' || *cursor == ',')) ++cursor;
        if (cursor >= limit) return false;
        char* end = nullptr;
        const double value = std::strtod(cursor, &end);
        if (end == cursor || end > limit || !std::isfinite(value)) return false;
        spectrum[i] = clamp01(static_cast<float>(value));
        cursor = end;
    }
    return true;
}

float high_band_average(const std::array<float, 32>& spectrum) {
    float sum = 0.0f;
    for (std::size_t i = 20; i < spectrum.size(); ++i) sum += spectrum[i];
    return sum / 12.0f;
}

float spectral_center(const std::array<float, 32>& spectrum) {
    double weighted = 0.0;
    double total = 0.0;
    for (std::size_t i = 0; i < spectrum.size(); ++i) {
        const double value = std::max(0.0f, spectrum[i]);
        const double position = static_cast<double>(i) / static_cast<double>(spectrum.size() - 1);
        weighted += value * position;
        total += value;
    }
    if (total <= 1e-9) return 0.0f;
    return clamp01(static_cast<float>(weighted / total));
}

} // namespace

EtherDNA make_ether_dna(const std::filesystem::path& source_audio, const AudioAnalysis& analysis) {
    EtherDNA dna;
    dna.source_audio = source_audio;
    dna.sample_rate = analysis.sample_rate;
    dna.channels = analysis.channels;
    dna.analyzed_windows = analysis.analyzed_windows;
    dna.duration_seconds = analysis.duration_seconds;
    dna.energy = clamp01(analysis.energy);
    dna.bass = clamp01(analysis.bass);
    dna.mid = clamp01(analysis.mid);
    dna.treble = clamp01(analysis.treble);
    dna.beat_peak = clamp01(analysis.beat_peak);
    dna.spectrum = analysis.spectrum;

    dna.low_end_weight = clamp01(dna.bass * 0.72f + dna.energy * 0.28f);
    const float high = high_band_average(dna.spectrum);
    dna.brightness = clamp01(dna.treble * 0.62f + high * 0.38f);
    dna.darkness = clamp01(1.0f - dna.brightness);
    dna.rhythmic_activity = clamp01(dna.beat_peak * 0.68f + dna.energy * 0.32f);
    dna.spectral_center = spectral_center(dna.spectrum);
    return dna;
}

std::filesystem::path ether_dna_sidecar_path(const std::filesystem::path& audio_path) {
    return std::filesystem::path(audio_path.wstring() + L".etherdna.json");
}

bool save_ether_dna(const EtherDNA& dna, const std::filesystem::path& path) noexcept {
    try {
        if (path.empty()) return false;
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;

        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"schema\": \"" << escape_json(dna.schema) << "\",\n";
        out << "  \"source_audio\": \"" << escape_json(path_utf8(dna.source_audio)) << "\",\n";
        out << "  \"sample_rate\": " << dna.sample_rate << ",\n";
        out << "  \"channels\": " << dna.channels << ",\n";
        out << "  \"analyzed_windows\": " << dna.analyzed_windows << ",\n";
        out << "  \"duration_seconds\": " << dna.duration_seconds << ",\n";
        out << "  \"energy\": " << dna.energy << ",\n";
        out << "  \"bass\": " << dna.bass << ",\n";
        out << "  \"mid\": " << dna.mid << ",\n";
        out << "  \"treble\": " << dna.treble << ",\n";
        out << "  \"beat_peak\": " << dna.beat_peak << ",\n";
        out << "  \"low_end_weight\": " << dna.low_end_weight << ",\n";
        out << "  \"brightness\": " << dna.brightness << ",\n";
        out << "  \"darkness\": " << dna.darkness << ",\n";
        out << "  \"rhythmic_activity\": " << dna.rhythmic_activity << ",\n";
        out << "  \"spectral_center\": " << dna.spectral_center << ",\n";
        out << "  \"spectrum\": [";
        for (std::size_t i = 0; i < dna.spectrum.size(); ++i) {
            if (i) out << ", ";
            out << dna.spectrum[i];
        }
        out << "]\n}";
        return static_cast<bool>(out);
    } catch (...) {
        return false;
    }
}

std::optional<EtherDNA> load_ether_dna(const std::filesystem::path& path) noexcept {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        const std::string text(
            std::istreambuf_iterator<char>{in},
            std::istreambuf_iterator<char>{});

        EtherDNA dna;
        const auto schema = string_after(text, "schema");
        if (!schema || *schema != "etherbeat.dna.v1") return std::nullopt;
        dna.schema = *schema;

        if (const auto source = string_after(text, "source_audio")) {
            dna.source_audio = std::filesystem::u8path(*source);
        }

        const auto sampleRate = number_after(text, "sample_rate");
        const auto channels = number_after(text, "channels");
        const auto analyzedWindows = number_after(text, "analyzed_windows");
        const auto duration = number_after(text, "duration_seconds");
        const auto energy = number_after(text, "energy");
        const auto bass = number_after(text, "bass");
        const auto mid = number_after(text, "mid");
        const auto treble = number_after(text, "treble");
        const auto beat = number_after(text, "beat_peak");
        const auto low = number_after(text, "low_end_weight");
        const auto bright = number_after(text, "brightness");
        const auto dark = number_after(text, "darkness");
        const auto rhythm = number_after(text, "rhythmic_activity");
        const auto center = number_after(text, "spectral_center");

        if (!sampleRate || !channels || !analyzedWindows || !duration || !energy ||
            !bass || !mid || !treble || !beat || !low || !bright || !dark ||
            !rhythm || !center || !parse_spectrum(text, dna.spectrum)) {
            return std::nullopt;
        }

        dna.sample_rate = static_cast<std::uint32_t>(std::max(0.0, *sampleRate));
        dna.channels = static_cast<std::uint32_t>(std::max(0.0, *channels));
        dna.analyzed_windows = static_cast<std::uint64_t>(std::max(0.0, *analyzedWindows));
        dna.duration_seconds = std::max(0.0, *duration);
        dna.energy = clamp01(static_cast<float>(*energy));
        dna.bass = clamp01(static_cast<float>(*bass));
        dna.mid = clamp01(static_cast<float>(*mid));
        dna.treble = clamp01(static_cast<float>(*treble));
        dna.beat_peak = clamp01(static_cast<float>(*beat));
        dna.low_end_weight = clamp01(static_cast<float>(*low));
        dna.brightness = clamp01(static_cast<float>(*bright));
        dna.darkness = clamp01(static_cast<float>(*dark));
        dna.rhythmic_activity = clamp01(static_cast<float>(*rhythm));
        dna.spectral_center = clamp01(static_cast<float>(*center));
        return dna;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<EtherDNA> load_ether_dna_for_audio(const std::filesystem::path& audio_path) noexcept {
    if (audio_path.empty()) return std::nullopt;
    return load_ether_dna(ether_dna_sidecar_path(audio_path));
}

std::string EtherDNA::conditioning_summary() const {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "reference DNA: energy " << energy
        << ", low-end weight " << low_end_weight
        << ", mid presence " << mid
        << ", brightness " << brightness
        << ", darkness " << darkness
        << ", rhythmic activity " << rhythmic_activity
        << ", spectral center " << spectral_center;
    return out.str();
}

} // namespace etherbeat
