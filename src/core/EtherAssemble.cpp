#include "etherbeat/EtherAssemble.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace etherbeat {
namespace {

void write_u16_le(std::ofstream& out, std::uint16_t value) {
    const char b[2]{
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu)};
    out.write(b, 2);
}

void write_u32_le(std::ofstream& out, std::uint32_t value) {
    const char b[4]{
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu)};
    out.write(b, 4);
}

std::string escape_json(const std::string& value) {
    std::ostringstream out;
    for (const char c : value) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << c; break;
        }
    }
    return out.str();
}

PcmAudio slice_audio(const PcmAudio& source, double start_seconds, double end_seconds) {
    if (!source.valid()) throw std::runtime_error("EtherAssemble source PCM is invalid");
    if (!std::isfinite(start_seconds) || !std::isfinite(end_seconds) ||
        start_seconds < 0.0 || end_seconds <= start_seconds) {
        throw std::runtime_error("EtherAssemble received invalid source section timing");
    }

    const auto frame_count = source.frame_count();
    const auto start_frame = std::min<std::size_t>(
        frame_count,
        static_cast<std::size_t>(std::llround(start_seconds * source.sample_rate)));
    const auto end_frame = std::min<std::size_t>(
        frame_count,
        static_cast<std::size_t>(std::llround(end_seconds * source.sample_rate)));
    if (end_frame <= start_frame) throw std::runtime_error("EtherAssemble section falls outside decoded source audio");

    const std::size_t first = start_frame * source.channels;
    const std::size_t last = end_frame * source.channels;
    PcmAudio result;
    result.sample_rate = source.sample_rate;
    result.channels = source.channels;
    result.samples.assign(source.samples.begin() + static_cast<std::ptrdiff_t>(first),
                          source.samples.begin() + static_cast<std::ptrdiff_t>(last));
    return result;
}

double expected_placeholder_duration(const ArrangementPlan& plan, const ArrangementSlot& target) {
    double same_kind_sum = 0.0;
    std::size_t same_kind_count = 0;
    double all_sum = 0.0;
    std::size_t all_count = 0;
    for (const auto& slot : plan.slots) {
        if (!slot.has_source_audio()) continue;
        const double duration = slot.source_end_seconds - slot.source_start_seconds;
        if (!(duration > 0.0)) continue;
        all_sum += duration;
        ++all_count;
        if (slot.kind == target.kind) {
            same_kind_sum += duration;
            ++same_kind_count;
        }
    }
    if (same_kind_count > 0) return same_kind_sum / static_cast<double>(same_kind_count);
    if (all_count > 0) return all_sum / static_cast<double>(all_count);
    return 8.0;
}

void validate_format(const PcmAudio& audio, std::uint32_t sample_rate, std::uint16_t channels) {
    if (!audio.valid()) throw std::runtime_error("EtherAssemble resolver returned invalid PCM audio");
    if (audio.sample_rate != sample_rate || audio.channels != channels) {
        throw std::runtime_error("EtherAssemble V0.1 requires matching sample rate/channel layout for every slot");
    }
}

void append_with_crossfade(
    std::vector<float>& output,
    const PcmAudio& segment,
    double requested_crossfade_seconds,
    AssembleSlotResult& slot_result) {

    const std::size_t channels = segment.channels;
    const std::size_t segment_frames = segment.frame_count();
    const std::size_t output_frames_before = output.size() / channels;
    const std::size_t requested_fade = static_cast<std::size_t>(std::llround(
        std::max(0.0, requested_crossfade_seconds) * segment.sample_rate));
    const std::size_t fade_frames = std::min({requested_fade, output_frames_before, segment_frames});

    const std::size_t slot_start_frame = output_frames_before - fade_frames;
    slot_result.output_start_seconds = static_cast<double>(slot_start_frame) / segment.sample_rate;

    if (fade_frames == 0) {
        output.insert(output.end(), segment.samples.begin(), segment.samples.end());
    } else {
        const std::size_t overlap_first = (output_frames_before - fade_frames) * channels;
        for (std::size_t frame = 0; frame < fade_frames; ++frame) {
            const float t = static_cast<float>(frame + 1u) / static_cast<float>(fade_frames + 1u);
            for (std::size_t ch = 0; ch < channels; ++ch) {
                const std::size_t dst = overlap_first + frame * channels + ch;
                const std::size_t src = frame * channels + ch;
                output[dst] = output[dst] * (1.0f - t) + segment.samples[src] * t;
            }
        }
        output.insert(
            output.end(),
            segment.samples.begin() + static_cast<std::ptrdiff_t>(fade_frames * channels),
            segment.samples.end());
    }

    slot_result.output_end_seconds =
        static_cast<double>(output.size() / channels) / segment.sample_rate;
}

void write_pcm16_wav(const std::filesystem::path& path, const PcmAudio& audio) {
    if (!audio.valid()) throw std::runtime_error("EtherAssemble cannot write invalid PCM");
    constexpr std::uint16_t bits_per_sample = 16;
    constexpr std::uint32_t bytes_per_sample = bits_per_sample / 8u;
    const std::uint32_t block_align = audio.channels * bytes_per_sample;
    const std::uint64_t data_size_64 = static_cast<std::uint64_t>(audio.frame_count()) * block_align;
    if (data_size_64 > std::numeric_limits<std::uint32_t>::max() - 36u) {
        throw std::runtime_error("EtherAssemble output exceeds RIFF/WAVE size limit");
    }

    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("EtherAssemble could not create output WAV");

    const auto data_size = static_cast<std::uint32_t>(data_size_64);
    out.write("RIFF", 4);
    write_u32_le(out, 36u + data_size);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    write_u32_le(out, 16u);
    write_u16_le(out, 1u);
    write_u16_le(out, audio.channels);
    write_u32_le(out, audio.sample_rate);
    write_u32_le(out, audio.sample_rate * block_align);
    write_u16_le(out, static_cast<std::uint16_t>(block_align));
    write_u16_le(out, bits_per_sample);
    out.write("data", 4);
    write_u32_le(out, data_size);

    for (float sample : audio.samples) {
        sample = std::clamp(sample, -1.0f, 1.0f);
        const auto pcm = static_cast<std::int16_t>(std::lrint(sample * 32767.0f));
        write_u16_le(out, static_cast<std::uint16_t>(pcm));
    }
    if (!out) throw std::runtime_error("EtherAssemble failed while writing output WAV");
}

void write_manifest(const ArrangementPlan& plan, const AssembleResult& result) {
    std::ofstream out(result.manifest_path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("EtherAssemble could not create manifest");
    out << std::fixed << std::setprecision(6);
    out << "{\n"
        << "  \"schema\": \"etherbeat.assemble.v1\",\n"
        << "  \"source_audio\": \"" << escape_json(plan.source_audio.generic_string()) << "\",\n"
        << "  \"arrangement_revision\": " << plan.revision << ",\n"
        << "  \"output_audio\": \"" << escape_json(result.audio_path.generic_string()) << "\",\n"
        << "  \"sample_rate\": " << result.sample_rate << ",\n"
        << "  \"channels\": " << result.channels << ",\n"
        << "  \"duration_seconds\": " << result.duration_seconds << ",\n"
        << "  \"crossfade_seconds\": " << result.crossfade_seconds << ",\n"
        << "  \"slots\": [\n";
    for (std::size_t i = 0; i < result.slots.size(); ++i) {
        const auto& slot = result.slots[i];
        out << "    {\"slot_id\": \"" << escape_json(slot.slot_id)
            << "\", \"label\": \"" << escape_json(slot.label)
            << "\", \"origin\": \"" << arrangement_origin_name(slot.origin)
            << "\", \"generated\": " << (slot.generated ? "true" : "false")
            << ", \"input_duration\": " << slot.input_duration_seconds
            << ", \"output_start\": " << slot.output_start_seconds
            << ", \"output_end\": " << slot.output_end_seconds << "}";
        if (i + 1u < result.slots.size()) out << ',';
        out << '\n';
    }
    out << "  ]\n}\n";
    if (!out) throw std::runtime_error("EtherAssemble failed while writing manifest");
}

} // namespace

std::filesystem::path ether_assemble_manifest_path(const std::filesystem::path& audio_path) {
    return std::filesystem::path(audio_path.wstring() + L".etherassemble.json");
}

AssembleResult EtherAssemble::render(
    const ArrangementPlan& plan,
    const std::filesystem::path& output_audio_path,
    const AssemblyAudioDecoder& decoder,
    const AssemblyPlaceholderResolver& placeholder_resolver,
    AssembleOptions options) const {

    if (plan.slots.empty()) throw std::runtime_error("EtherAssemble arrangement plan has no slots");
    if (output_audio_path.empty()) throw std::runtime_error("EtherAssemble output path is empty");
    if (!decoder) throw std::runtime_error("EtherAssemble requires an audio decoder");
    options.crossfade_seconds = std::clamp(options.crossfade_seconds, 0.0, 0.250);

    std::optional<PcmAudio> decoded_source;
    std::uint32_t target_rate = 0;
    std::uint16_t target_channels = 0;
    std::vector<float> output_samples;

    AssembleResult result;
    result.audio_path = output_audio_path;
    result.manifest_path = ether_assemble_manifest_path(output_audio_path);
    result.crossfade_seconds = options.crossfade_seconds;

    for (const auto& slot : plan.slots) {
        PcmAudio segment;
        bool generated = false;

        if (slot.has_source_audio()) {
            if (!decoded_source) {
                decoded_source = decoder(plan.source_audio);
                if (!decoded_source->valid()) throw std::runtime_error("EtherAssemble could not decode source audio");
            }
            segment = slice_audio(*decoded_source, slot.source_start_seconds, slot.source_end_seconds);
        } else {
            if (!placeholder_resolver) {
                if (options.require_all_placeholders) {
                    throw std::runtime_error("EtherAssemble arrangement contains unresolved placeholder: " + slot.label);
                }
                continue;
            }
            const auto resolved = placeholder_resolver(slot, expected_placeholder_duration(plan, slot));
            if (!resolved || !resolved->valid()) {
                if (options.require_all_placeholders) {
                    throw std::runtime_error("EtherAssemble placeholder resolver failed: " + slot.label);
                }
                continue;
            }
            segment = *resolved;
            generated = true;
        }

        if (target_rate == 0) {
            target_rate = segment.sample_rate;
            target_channels = segment.channels;
        } else {
            validate_format(segment, target_rate, target_channels);
        }

        AssembleSlotResult slot_result;
        slot_result.slot_id = slot.slot_id;
        slot_result.label = slot.label;
        slot_result.origin = slot.origin;
        slot_result.generated = generated;
        slot_result.input_duration_seconds = segment.duration_seconds();
        append_with_crossfade(output_samples, segment, options.crossfade_seconds, slot_result);
        result.slots.push_back(std::move(slot_result));
    }

    if (result.slots.empty() || output_samples.empty()) {
        throw std::runtime_error("EtherAssemble produced no audio slots");
    }

    PcmAudio assembled;
    assembled.sample_rate = target_rate;
    assembled.channels = target_channels;
    assembled.samples = std::move(output_samples);
    result.sample_rate = target_rate;
    result.channels = target_channels;
    result.duration_seconds = assembled.duration_seconds();

    write_pcm16_wav(result.audio_path, assembled);
    write_manifest(plan, result);
    return result;
}

} // namespace etherbeat
