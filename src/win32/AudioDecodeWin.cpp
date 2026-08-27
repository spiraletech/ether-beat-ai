#include "etherbeat/AudioDecodeWin.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

namespace etherbeat {
namespace {

void check_hr(HRESULT hr, const char* message) {
    if (FAILED(hr)) throw std::runtime_error(message);
}

class MfSession {
public:
    MfSession() {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        co_initialized_ = SUCCEEDED(co);
        check_hr(MFStartup(MF_VERSION, MFSTARTUP_LITE), "Media Foundation startup failed");
        mf_started_ = true;
    }

    ~MfSession() {
        if (mf_started_) MFShutdown();
        if (co_initialized_) CoUninitialize();
    }

private:
    bool mf_started_{false};
    bool co_initialized_{false};
};

} // namespace

PcmAudio decode_audio_pcm_file(const std::filesystem::path& path) {
    if (path.empty()) throw std::runtime_error("PCM decode path is empty");
    MfSession session;

    ComPtr<IMFSourceReader> reader;
    check_hr(
        MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader),
        "Could not open audio source for PCM decode");

    ComPtr<IMFMediaType> requested;
    check_hr(MFCreateMediaType(&requested), "Could not create PCM media type");
    check_hr(requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Could not set PCM major type");
    check_hr(requested->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float), "Could not request float PCM");
    check_hr(
        reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, requested.Get()),
        "Could not configure float PCM decoder");

    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    ComPtr<IMFMediaType> active;
    check_hr(
        reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &active),
        "Could not read decoded PCM media type");

    UINT32 sample_rate = 0;
    UINT32 channels = 0;
    check_hr(active->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sample_rate), "Decoded PCM sample rate missing");
    check_hr(active->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels), "Decoded PCM channel count missing");
    if (sample_rate == 0 || channels == 0 || channels > 32u) {
        throw std::runtime_error("Decoded PCM format is invalid");
    }

    PcmAudio result;
    result.sample_rate = sample_rate;
    result.channels = static_cast<std::uint16_t>(channels);

    while (true) {
        DWORD stream_index = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        check_hr(
            reader->ReadSample(
                MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                0,
                &stream_index,
                &flags,
                &timestamp,
                &sample),
            "Media Foundation PCM decode failed");

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;

        ComPtr<IMFMediaBuffer> buffer;
        check_hr(sample->ConvertToContiguousBuffer(&buffer), "Could not flatten decoded PCM buffer");
        BYTE* bytes = nullptr;
        DWORD max_length = 0;
        DWORD current_length = 0;
        check_hr(buffer->Lock(&bytes, &max_length, &current_length), "Could not lock decoded PCM buffer");

        if (current_length % sizeof(float) != 0u) {
            buffer->Unlock();
            throw std::runtime_error("Decoded float PCM buffer is misaligned");
        }

        const auto* begin = reinterpret_cast<const float*>(bytes);
        const std::size_t count = current_length / sizeof(float);
        result.samples.insert(result.samples.end(), begin, begin + count);
        buffer->Unlock();
    }

    if (!result.valid()) throw std::runtime_error("Decoded source produced no PCM audio");
    return result;
}

} // namespace etherbeat
