#include "etherbeat/MockWaveBackend.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

namespace etherbeat {
namespace {

constexpr std::uint32_t kSampleRate = 48'000;
constexpr std::uint16_t kChannels = 2;
constexpr std::uint16_t kBitsPerSample = 16;

void write_u16_le(std::ofstream& out, std::uint16_t value) {
    const char bytes[2] = {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8u) & 0xFFu)
    };
    out.write(bytes, 2);
}

void write_u32_le(std::ofstream& out, std::uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8u) & 0xFFu),
        static_cast<char>((value >> 16u) & 0xFFu),
        static_cast<char>((value >> 24u) & 0xFFu)
    };
    out.write(bytes, 4);
}

std::string json_escape(const std::string& value) {
    std::ostringstream escaped;
    for (const char c : value) {
        switch (c) {
        case '\\': escaped << "\\\\"; break;
        case '"': escaped << "\\\""; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default: escaped << c; break;
        }
    }
    return escaped.str();
}

std::uint64_t resolve_seed(std::uint64_t requested) {
    if (requested != 0) {
        return requested;
    }

    std::random_device random_device;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return (static_cast<std::uint64_t>(random_device()) << 32u) ^ now;
}

std::string mode_name(GenerationMode mode) {
    switch (mode) {
    case GenerationMode::TextToInstrumental: return "text_to_instrumental";
    case GenerationMode::Variation: return "variation";
    case GenerationMode::Extend: return "extend";
    case GenerationMode::AudioToAudio: return "audio_to_audio";
    }
    return "unknown";
}

} // namespace

std::string_view MockWaveBackend::name() const noexcept {
    return "mock-wave-48k";
}

GenerationArtifact MockWaveBackend::generate(
    const GenerationRequest& request,
    const std::filesystem::path& output_directory) {

    std::filesystem::create_directories(output_directory);

    const std::uint64_t seed = resolve_seed(request.seed);
    const auto sample_frames_64 = static_cast<std::uint64_t>(
        request.duration_seconds * static_cast<double>(kSampleRate));
    const auto sample_frames = std::max<std::uint64_t>(1, sample_frames_64);

    constexpr std::uint32_t bytes_per_sample = kBitsPerSample / 8u;
    constexpr std::uint32_t block_align = kChannels * bytes_per_sample;

    const std::uint64_t data_size_64 = sample_frames * block_align;
    if (data_size_64 > std::numeric_limits<std::uint32_t>::max() - 36u) {
        throw std::runtime_error("Requested WAV is too large for RIFF/WAVE output");
    }

    const auto data_size = static_cast<std::uint32_t>(data_size_64);

    std::ostringstream stem;
    stem << "etherbeat_" << std::hex << seed;

    const auto audio_path = output_directory / (stem.str() + ".wav");
    const auto metadata_path = output_directory / (stem.str() + ".etherbeat.json");

    std::ofstream wav(audio_path, std::ios::binary);
    if (!wav) {
        throw std::runtime_error("Could not create WAV output");
    }

    wav.write("RIFF", 4);
    write_u32_le(wav, 36u + data_size);
    wav.write("WAVE", 4);

    wav.write("fmt ", 4);
    write_u32_le(wav, 16u);
    write_u16_le(wav, 1u); // PCM
    write_u16_le(wav, kChannels);
    write_u32_le(wav, kSampleRate);
    write_u32_le(wav, kSampleRate * block_align);
    write_u16_le(wav, static_cast<std::uint16_t>(block_align));
    write_u16_le(wav, kBitsPerSample);

    wav.write("data", 4);
    write_u32_le(wav, data_size);

    constexpr std::size_t chunk_size = 16 * 1024;
    const char silence[chunk_size]{};
    std::uint64_t remaining = data_size;
    while (remaining > 0) {
        const auto chunk = static_cast<std::streamsize>(
            std::min<std::uint64_t>(remaining, chunk_size));
        wav.write(silence, chunk);
        remaining -= static_cast<std::uint64_t>(chunk);
    }

    if (!wav) {
        throw std::runtime_error("Failed while writing WAV output");
    }

    std::ofstream metadata(metadata_path);
    if (!metadata) {
        throw std::runtime_error("Could not create generation metadata");
    }

    metadata
        << "{\n"
        << "  \"schema\": \"etherbeat.generation.v1\",\n"
        << "  \"backend\": \"" << name() << "\",\n"
        << "  \"seed\": " << seed << ",\n"
        << "  \"mode\": \"" << mode_name(request.mode) << "\",\n"
        << "  \"duration_seconds\": " << std::fixed << std::setprecision(3)
        << request.duration_seconds << ",\n"
        << "  \"bpm\": " << request.bpm << ",\n"
        << "  \"key\": \"" << json_escape(request.key) << "\",\n"
        << "  \"mutation_amount\": " << request.mutation_amount << ",\n"
        << "  \"prompt\": \"" << json_escape(request.prompt) << "\",\n"
        << "  \"audio_format\": {\n"
        << "    \"sample_rate\": " << kSampleRate << ",\n"
        << "    \"channels\": " << kChannels << ",\n"
        << "    \"bits_per_sample\": " << kBitsPerSample << "\n"
        << "  }\n"
        << "}\n";

    return GenerationArtifact{
        .audio_path = audio_path,
        .metadata_path = metadata_path,
        .backend_name = std::string{name()},
        .resolved_seed = seed
    };
}

} // namespace etherbeat
