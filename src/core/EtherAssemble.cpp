#include "etherbeat/EtherAssemble.hpp"
#include "etherbeat/EtherSeam.hpp"

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
    const char b[2]{static_cast<char>(value & 0xffu), static_cast<char>((value >> 8u) & 0xffu)};
    out.write(b, 2);
}

void write_u32_le(std::ofstream& out, std::uint32_t value) {
    const char b[4]{static_cast<char>(value & 0xffu), static_cast<char>((value >> 8u) & 0xffu),
                    static_cast<char>((value >> 16u) & 0xffu), static_cast<char>((value >> 24u) & 0xffu)};
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
    if (!std::isfinite(start_seconds) || !std::isfinite(end_seconds) || start_seconds < 0.0 || end_seconds <= start_seconds) {
        throw std::runtime_error("EtherAssemble received invalid source section timing");
    }
    const auto frames = source.frame_count();
    const auto first_frame = std::min<std::size_t>(frames, static_cast<std::size_t>(std::llround(start_seconds * source.sample_rate)));
    const auto last_frame = std::min<std::size_t>(frames, static_cast<std::size_t>(std::llround(end_seconds * source.sample_rate)));
    if (last_frame <= first_frame) throw std::runtime_error("EtherAssemble section falls outside decoded source audio");

    PcmAudio result;
    result.sample_rate = source.sample_rate;
    result.channels = source.channels;
    const std::size_t first = first_frame * source.channels;
    const std::size_t last = last_frame * source.channels;
    result.samples.assign(source.samples.begin() + static_cast<std::ptrdiff_t>(first),
                          source.samples.begin() + static_cast<std::ptrdiff_t>(last));
    return result;
}

double expected_placeholder_duration(const ArrangementPlan& plan, const ArrangementSlot& target) {
    double same_sum = 0.0, all_sum = 0.0;
    std::size_t same_count = 0, all_count = 0;
    for (const auto& slot : plan.slots) {
        if (!slot.has_source_audio()) continue;
        const double d = slot.source_end_seconds - slot.source_start_seconds;
        if (!(d > 0.0)) continue;
        all_sum += d; ++all_count;
        if (slot.kind == target.kind) { same_sum += d; ++same_count; }
    }
    if (same_count) return same_sum / static_cast<double>(same_count);
    if (all_count) return all_sum / static_cast<double>(all_count);
    return 8.0;
}

PcmAudio convert_channels(const PcmAudio& source, std::uint16_t target_channels) {
    if (!source.valid() || target_channels == 0) throw std::runtime_error("EtherAssemble channel conversion input is invalid");
    if (source.channels == target_channels) return source;

    PcmAudio result;
    result.sample_rate = source.sample_rate;
    result.channels = target_channels;
    result.samples.resize(source.frame_count() * target_channels);

    for (std::size_t frame = 0; frame < source.frame_count(); ++frame) {
        const std::size_t src = frame * source.channels;
        const std::size_t dst = frame * target_channels;
        float average = 0.0f;
        for (std::size_t ch = 0; ch < source.channels; ++ch) average += source.samples[src + ch];
        average /= static_cast<float>(source.channels);

        if (target_channels == 1) {
            result.samples[dst] = average;
        } else if (source.channels == 1) {
            for (std::size_t ch = 0; ch < target_channels; ++ch) result.samples[dst + ch] = source.samples[src];
        } else {
            for (std::size_t ch = 0; ch < target_channels; ++ch) {
                result.samples[dst + ch] = ch < source.channels ? source.samples[src + ch] : average;
            }
        }
    }
    return result;
}

PcmAudio convert_sample_rate(const PcmAudio& source, std::uint32_t target_rate) {
    if (!source.valid() || target_rate == 0) throw std::runtime_error("EtherAssemble sample-rate conversion input is invalid");
    if (source.sample_rate == target_rate) return source;

    const double ratio = static_cast<double>(target_rate) / static_cast<double>(source.sample_rate);
    const std::size_t target_frames = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(source.frame_count() * ratio)));
    PcmAudio result;
    result.sample_rate = target_rate;
    result.channels = source.channels;
    result.samples.resize(target_frames * result.channels);

    for (std::size_t frame = 0; frame < target_frames; ++frame) {
        const double src_pos = static_cast<double>(frame) / ratio;
        const std::size_t a = std::min<std::size_t>(source.frame_count() - 1u, static_cast<std::size_t>(src_pos));
        const std::size_t b = std::min<std::size_t>(source.frame_count() - 1u, a + 1u);
        const float t = static_cast<float>(src_pos - static_cast<double>(a));
        for (std::size_t ch = 0; ch < result.channels; ++ch) {
            const float va = source.samples[a * result.channels + ch];
            const float vb = source.samples[b * result.channels + ch];
            result.samples[frame * result.channels + ch] = va + (vb - va) * t;
        }
    }
    return result;
}

PcmAudio normalize_format(PcmAudio audio, std::uint32_t target_rate, std::uint16_t target_channels) {
    if (!audio.valid()) throw std::runtime_error("EtherAssemble resolver returned invalid PCM audio");
    audio = convert_channels(audio, target_channels);
    audio = convert_sample_rate(audio, target_rate);
    return audio;
}

void append_with_crossfade(std::vector<float>& output, const PcmAudio& segment,
                           double requested_crossfade_seconds, AssembleSlotResult& slot_result) {
    const std::size_t channels = segment.channels;
    const std::size_t segment_frames = segment.frame_count();
    const std::size_t output_frames_before = output.size() / channels;
    const std::size_t requested_fade = static_cast<std::size_t>(std::llround(
        std::max(0.0, requested_crossfade_seconds) * segment.sample_rate));
    const std::size_t fade_frames = std::min({requested_fade, output_frames_before, segment_frames});
    const std::size_t slot_start_frame = output_frames_before - fade_frames;
    slot_result.output_start_seconds = static_cast<double>(slot_start_frame) / segment.sample_rate;
    slot_result.applied_crossfade_seconds = static_cast<double>(fade_frames) / segment.sample_rate;

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
        output.insert(output.end(), segment.samples.begin() + static_cast<std::ptrdiff_t>(fade_frames * channels), segment.samples.end());
    }
    slot_result.output_end_seconds = static_cast<double>(output.size() / channels) / segment.sample_rate;
}

void write_pcm16_wav(const std::filesystem::path& path, const PcmAudio& audio) {
    if (!audio.valid()) throw std::runtime_error("EtherAssemble cannot write invalid PCM");
    constexpr std::uint16_t bits_per_sample = 16;
    constexpr std::uint32_t bytes_per_sample = 2;
    const std::uint32_t block_align = audio.channels * bytes_per_sample;
    const std::uint64_t data_size_64 = static_cast<std::uint64_t>(audio.frame_count()) * block_align;
    if (data_size_64 > std::numeric_limits<std::uint32_t>::max() - 36u) throw std::runtime_error("EtherAssemble output exceeds RIFF/WAVE size limit");

    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("EtherAssemble could not create output WAV");
    const auto data_size = static_cast<std::uint32_t>(data_size_64);
    out.write("RIFF", 4); write_u32_le(out, 36u + data_size); out.write("WAVE", 4);
    out.write("fmt ", 4); write_u32_le(out, 16u); write_u16_le(out, 1u); write_u16_le(out, audio.channels);
    write_u32_le(out, audio.sample_rate); write_u32_le(out, audio.sample_rate * block_align);
    write_u16_le(out, static_cast<std::uint16_t>(block_align)); write_u16_le(out, bits_per_sample);
    out.write("data", 4); write_u32_le(out, data_size);
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
    out << std::fixed << std::setprecision(6)
        << "{\n  \"schema\": \"etherbeat.assemble.v2\",\n"
        << "  \"source_audio\": \"" << escape_json(plan.source_audio.generic_string()) << "\",\n"
        << "  \"arrangement_revision\": " << plan.revision << ",\n"
        << "  \"output_audio\": \"" << escape_json(result.audio_path.generic_string()) << "\",\n"
        << "  \"sample_rate\": " << result.sample_rate << ",\n"
        << "  \"channels\": " << result.channels << ",\n"
        << "  \"duration_seconds\": " << result.duration_seconds << ",\n"
        << "  \"fixed_crossfade_seconds\": " << result.crossfade_seconds << ",\n"
        << "  \"adaptive_seams\": " << (result.adaptive_seams ? "true" : "false") << ",\n"
        << "  \"average_seam_score\": " << result.average_seam_score << ",\n"
        << "  \"max_seam_score\": " << result.max_seam_score << ",\n"
        << "  \"severe_seam_count\": " << result.severe_seam_count << ",\n"
        << "  \"slots\": [\n";
    for (std::size_t i = 0; i < result.slots.size(); ++i) {
        const auto& slot = result.slots[i];
        out << "    {\"slot_id\": \"" << escape_json(slot.slot_id)
            << "\", \"label\": \"" << escape_json(slot.label)
            << "\", \"origin\": \"" << arrangement_origin_name(slot.origin)
            << "\", \"generated\": " << (slot.generated ? "true" : "false")
            << ", \"input_duration\": " << slot.input_duration_seconds
            << ", \"output_start\": " << slot.output_start_seconds
            << ", \"output_end\": " << slot.output_end_seconds
            << ", \"seam_analyzed\": " << (slot.seam_analyzed ? "true" : "false")
            << ", \"seam_score\": " << slot.seam_score
            << ", \"rms_jump\": " << slot.rms_jump
            << ", \"spectral_jump\": " << slot.spectral_jump
            << ", \"dc_jump\": " << slot.dc_jump
            << ", \"transient_collision\": " << slot.transient_collision
            << ", \"sample_jump\": " << slot.sample_jump
            << ", \"applied_crossfade_seconds\": " << slot.applied_crossfade_seconds
            << ", \"severe_seam\": " << (slot.severe_seam ? "true" : "false") << "}";
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

AssembleResult EtherAssemble::render(const ArrangementPlan& plan,
                                     const std::filesystem::path& output_audio_path,
                                     const AssemblyAudioDecoder& decoder,
                                     const AssemblyPlaceholderResolver& placeholder_resolver,
                                     AssembleOptions options) const {
    if (plan.slots.empty()) throw std::runtime_error("EtherAssemble arrangement plan has no slots");
    if (output_audio_path.empty()) throw std::runtime_error("EtherAssemble output path is empty");
    if (!decoder) throw std::runtime_error("EtherAssemble requires an audio decoder");
    options.crossfade_seconds = std::clamp(options.crossfade_seconds, 0.0, 0.250);
    options.min_crossfade_seconds = std::clamp(options.min_crossfade_seconds, 0.0, 0.250);
    options.max_crossfade_seconds = std::clamp(options.max_crossfade_seconds,
                                                options.min_crossfade_seconds,
                                                0.250);
    options.severe_seam_score = std::clamp(options.severe_seam_score, 0.0, 1.0);

    std::optional<PcmAudio> decoded_source;
    std::optional<PcmAudio> previous_segment;
    std::uint32_t target_rate = 0;
    std::uint16_t target_channels = 0;

    const bool needs_source = std::any_of(plan.slots.begin(), plan.slots.end(), [](const ArrangementSlot& slot) {
        return slot.has_source_audio();
    });
    if (needs_source) {
        decoded_source = decoder(plan.source_audio);
        if (!decoded_source->valid()) throw std::runtime_error("EtherAssemble could not decode source audio");
        target_rate = decoded_source->sample_rate;
        target_channels = decoded_source->channels;
    }

    std::vector<float> output_samples;
    AssembleResult result;
    result.audio_path = output_audio_path;
    result.manifest_path = ether_assemble_manifest_path(output_audio_path);
    result.crossfade_seconds = options.crossfade_seconds;
    result.adaptive_seams = options.adaptive_seams;

    double seam_score_sum = 0.0;
    std::size_t seam_count = 0;

    for (const auto& slot : plan.slots) {
        PcmAudio segment;
        bool generated = false;
        if (slot.has_source_audio()) {
            segment = slice_audio(*decoded_source, slot.source_start_seconds, slot.source_end_seconds);
        } else {
            if (!placeholder_resolver) {
                if (options.require_all_placeholders) throw std::runtime_error("EtherAssemble arrangement contains unresolved placeholder: " + slot.label);
                continue;
            }
            const auto resolved = placeholder_resolver(slot, expected_placeholder_duration(plan, slot));
            if (!resolved || !resolved->valid()) {
                if (options.require_all_placeholders) throw std::runtime_error("EtherAssemble placeholder resolver failed: " + slot.label);
                continue;
            }
            segment = *resolved;
            generated = true;
        }

        if (target_rate == 0) {
            target_rate = segment.sample_rate;
            target_channels = segment.channels;
        }
        segment = normalize_format(std::move(segment), target_rate, target_channels);

        AssembleSlotResult slot_result;
        slot_result.slot_id = slot.slot_id;
        slot_result.label = slot.label;
        slot_result.origin = slot.origin;
        slot_result.generated = generated;
        slot_result.input_duration_seconds = segment.duration_seconds();

        double requested_crossfade = 0.0;
        if (previous_segment) {
            const auto seam = analyze_seam(
                *previous_segment,
                segment,
                SeamOptions{
                    .analysis_window_seconds = 0.030,
                    .min_crossfade_seconds = options.min_crossfade_seconds,
                    .max_crossfade_seconds = options.max_crossfade_seconds,
                    .severe_score = options.severe_seam_score});

            if (seam.ready) {
                slot_result.seam_analyzed = true;
                slot_result.seam_score = seam.seam_score;
                slot_result.rms_jump = seam.rms_jump;
                slot_result.spectral_jump = seam.spectral_jump;
                slot_result.dc_jump = seam.dc_jump;
                slot_result.transient_collision = seam.transient_collision;
                slot_result.sample_jump = seam.sample_jump;
                slot_result.severe_seam = seam.severe;

                seam_score_sum += seam.seam_score;
                ++seam_count;
                result.max_seam_score = std::max(result.max_seam_score, seam.seam_score);
                if (seam.severe) ++result.severe_seam_count;

                if (options.reject_severe_seams && seam.severe) {
                    throw std::runtime_error("EtherSeam rejected high-risk boundary before slot: " + slot.label);
                }
                requested_crossfade = options.adaptive_seams
                    ? seam.recommended_crossfade_seconds
                    : options.crossfade_seconds;
            } else {
                requested_crossfade = options.crossfade_seconds;
            }
        }

        append_with_crossfade(output_samples, segment, requested_crossfade, slot_result);
        result.slots.push_back(std::move(slot_result));
        previous_segment = std::move(segment);
    }

    if (result.slots.empty() || output_samples.empty()) throw std::runtime_error("EtherAssemble produced no audio slots");
    PcmAudio assembled{target_rate, target_channels, std::move(output_samples)};
    result.sample_rate = target_rate;
    result.channels = target_channels;
    result.duration_seconds = assembled.duration_seconds();
    result.average_seam_score = seam_count == 0 ? 0.0 : seam_score_sum / static_cast<double>(seam_count);
    write_pcm16_wav(result.audio_path, assembled);
    write_manifest(plan, result);
    return result;
}

} // namespace etherbeat
