#include "etherbeat/AudioAnalysis.hpp"
#include "etherbeat/EtherDNA.hpp"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>

namespace etherbeat {
namespace {

constexpr std::size_t kFFTSize = 1024;
constexpr std::size_t kSpectrumBands = 32;
constexpr std::uint64_t kMaxWindows = 30000;
constexpr float kPi = 3.14159265358979323846f;

struct MfLifetime {
    HRESULT status{MFStartup(MF_VERSION)};
    ~MfLifetime() {
        if (SUCCEEDED(status)) MFShutdown();
    }
};

template <typename T>
void release(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

void fft(std::array<std::complex<float>, kFFTSize>& values) {
    for (std::size_t i = 1, j = 0; i < kFFTSize; ++i) {
        std::size_t bit = kFFTSize >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(values[i], values[j]);
    }

    for (std::size_t length = 2; length <= kFFTSize; length <<= 1) {
        const float angle = -2.0f * kPi / static_cast<float>(length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < kFFTSize; i += length) {
            std::complex<float> phase(1.0f, 0.0f);
            for (std::size_t j = 0; j < length / 2; ++j) {
                const auto a = values[i + j];
                const auto b = values[i + j + length / 2] * phase;
                values[i + j] = a + b;
                values[i + j + length / 2] = a - b;
                phase *= step;
            }
        }
    }
}

float frequency_band(
    const std::array<std::complex<float>, kFFTSize>& bins,
    std::uint32_t sampleRate,
    float lowHz,
    float highHz) {

    const int half = static_cast<int>(kFFTSize / 2);
    const int low = std::clamp(
        static_cast<int>(lowHz * static_cast<float>(kFFTSize) / static_cast<float>(sampleRate)),
        1, half - 1);
    const int high = std::clamp(
        static_cast<int>(highHz * static_cast<float>(kFFTSize) / static_cast<float>(sampleRate)),
        low + 1, half);

    double sum = 0.0;
    for (int i = low; i < high; ++i) sum += std::abs(bins[static_cast<std::size_t>(i)]);
    return static_cast<float>(sum / static_cast<double>(std::max(1, high - low)));
}

struct WindowFeatures {
    float energy{};
    float bass{};
    float mid{};
    float treble{};
    std::array<float, kSpectrumBands> spectrum{};
};

WindowFeatures analyze_window(const std::array<float, kFFTSize>& samples, std::uint32_t sampleRate) {
    WindowFeatures result;
    std::array<std::complex<float>, kFFTSize> bins{};

    double sumSquares = 0.0;
    for (std::size_t i = 0; i < kFFTSize; ++i) {
        const float sample = samples[i];
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
        const float hann = 0.5f - 0.5f * std::cos(
            2.0f * kPi * static_cast<float>(i) / static_cast<float>(kFFTSize - 1));
        bins[i] = std::complex<float>(sample * hann, 0.0f);
    }

    result.energy = static_cast<float>(std::sqrt(sumSquares / static_cast<double>(kFFTSize)));
    fft(bins);

    result.bass = frequency_band(bins, sampleRate, 40.0f, 180.0f);
    result.mid = frequency_band(bins, sampleRate, 180.0f, 2200.0f);
    result.treble = frequency_band(bins, sampleRate, 2200.0f, 12000.0f);

    constexpr float minHz = 40.0f;
    constexpr float maxHz = 12000.0f;
    const float ratio = maxHz / minHz;
    for (std::size_t band = 0; band < kSpectrumBands; ++band) {
        const float t0 = static_cast<float>(band) / static_cast<float>(kSpectrumBands);
        const float t1 = static_cast<float>(band + 1) / static_cast<float>(kSpectrumBands);
        const float lo = minHz * std::pow(ratio, t0);
        const float hi = minHz * std::pow(ratio, t1);
        result.spectrum[band] = frequency_band(bins, sampleRate, lo, hi);
    }

    return result;
}

} // namespace

AudioAnalysis analyze_audio_file(const std::filesystem::path& path) {
    AudioAnalysis result;
    if (path.empty() || !std::filesystem::exists(path)) {
        result.error = "Reference audio does not exist";
        return result;
    }

    MfLifetime mf;
    if (FAILED(mf.status)) {
        result.error = "Media Foundation could not start";
        return result;
    }

    IMFSourceReader* reader = nullptr;
    IMFMediaType* requestedType = nullptr;
    IMFMediaType* actualType = nullptr;

    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
    if (FAILED(hr) || !reader) {
        result.error = "Media Foundation could not open this audio file";
        return result;
    }

    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    hr = MFCreateMediaType(&requestedType);
    if (SUCCEEDED(hr)) {
        requestedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        requestedType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, requestedType);
    }

    if (FAILED(hr)) {
        result.error = "Reference audio could not be converted to PCM";
        release(requestedType);
        release(reader);
        return result;
    }

    hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actualType);
    UINT32 channels = 0;
    UINT32 sampleRate = 0;
    UINT32 bits = 0;
    if (SUCCEEDED(hr) && actualType) {
        actualType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        actualType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
        actualType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
    }

    if (channels == 0 || sampleRate == 0 || bits != 16) {
        result.error = "Reference decoder returned an unsupported PCM format";
        release(actualType);
        release(requestedType);
        release(reader);
        return result;
    }

    result.sample_rate = sampleRate;
    result.channels = channels;

    std::array<float, kFFTSize> window{};
    std::size_t fill = 0;
    std::uint64_t pcmFrames = 0;
    std::uint64_t windows = 0;

    double energySum = 0.0;
    double bassSum = 0.0;
    double midSum = 0.0;
    double trebleSum = 0.0;
    float maxEnergy = std::numeric_limits<float>::epsilon();
    float maxBass = std::numeric_limits<float>::epsilon();
    float maxMid = std::numeric_limits<float>::epsilon();
    float maxTreble = std::numeric_limits<float>::epsilon();
    float bassEnvelope = 0.0f;
    float beatPeak = 0.0f;
    std::array<double, kSpectrumBands> spectrumSum{};

    bool done = false;
    while (!done && windows < kMaxWindows) {
        DWORD stream = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample* sample = nullptr;

        hr = reader->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0,
            &stream,
            &flags,
            &timestamp,
            &sample);

        if (FAILED(hr)) {
            release(sample);
            result.error = "Media Foundation stopped while reading reference audio";
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) done = true;

        if (sample) {
            IMFMediaBuffer* buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) && buffer) {
                BYTE* data = nullptr;
                DWORD maxLength = 0;
                DWORD currentLength = 0;
                if (SUCCEEDED(buffer->Lock(&data, &maxLength, &currentLength)) && data) {
                    const auto* pcm = reinterpret_cast<const std::int16_t*>(data);
                    const std::size_t frameCount = currentLength / (sizeof(std::int16_t) * channels);
                    pcmFrames += frameCount;

                    for (std::size_t frame = 0; frame < frameCount && windows < kMaxWindows; ++frame) {
                        float mono = 0.0f;
                        for (UINT32 channel = 0; channel < channels; ++channel) {
                            mono += static_cast<float>(pcm[frame * channels + channel]) / 32768.0f;
                        }
                        mono /= static_cast<float>(channels);
                        window[fill++] = mono;

                        if (fill == kFFTSize) {
                            const WindowFeatures features = analyze_window(window, sampleRate);
                            energySum += features.energy;
                            bassSum += features.bass;
                            midSum += features.mid;
                            trebleSum += features.treble;
                            maxEnergy = std::max(maxEnergy, features.energy);
                            maxBass = std::max(maxBass, features.bass);
                            maxMid = std::max(maxMid, features.mid);
                            maxTreble = std::max(maxTreble, features.treble);

                            for (std::size_t i = 0; i < kSpectrumBands; ++i) {
                                spectrumSum[i] += features.spectrum[i];
                            }

                            if (bassEnvelope > std::numeric_limits<float>::epsilon()) {
                                const float ratioNow = features.bass / bassEnvelope;
                                beatPeak = std::max(beatPeak, std::clamp((ratioNow - 1.02f) * 2.8f, 0.0f, 1.0f));
                            }
                            bassEnvelope = bassEnvelope * 0.92f + features.bass * 0.08f;

                            ++windows;
                            fill = 0;
                        }
                    }
                    buffer->Unlock();
                }
                release(buffer);
            }
            release(sample);
        }
    }

    release(actualType);
    release(requestedType);
    release(reader);

    if (windows == 0) {
        if (result.error.empty()) result.error = "Reference audio contained no analyzable PCM frames";
        return result;
    }

    const double invWindows = 1.0 / static_cast<double>(windows);
    const float avgEnergy = static_cast<float>(energySum * invWindows);
    const float avgBass = static_cast<float>(bassSum * invWindows);
    const float avgMid = static_cast<float>(midSum * invWindows);
    const float avgTreble = static_cast<float>(trebleSum * invWindows);

    result.energy = std::clamp(std::sqrt(avgEnergy / maxEnergy), 0.0f, 1.0f);
    result.bass = std::clamp(std::sqrt(avgBass / maxBass), 0.0f, 1.0f);
    result.mid = std::clamp(std::sqrt(avgMid / maxMid), 0.0f, 1.0f);
    result.treble = std::clamp(std::sqrt(avgTreble / maxTreble), 0.0f, 1.0f);
    result.beat_peak = beatPeak;

    double maxSpectrum = std::numeric_limits<double>::epsilon();
    for (const double sum : spectrumSum) maxSpectrum = std::max(maxSpectrum, sum * invWindows);
    for (std::size_t i = 0; i < kSpectrumBands; ++i) {
        const double average = spectrumSum[i] * invWindows;
        result.spectrum[i] = std::clamp(
            static_cast<float>(std::sqrt(average / maxSpectrum)), 0.0f, 1.0f);
    }

    result.analyzed_windows = windows;
    result.duration_seconds = static_cast<double>(pcmFrames) / static_cast<double>(sampleRate);
    result.ready = true;
    result.error.clear();

    const EtherDNA dna = make_ether_dna(path, result);
    (void)save_ether_dna(dna, ether_dna_sidecar_path(path));

    return result;
}

} // namespace etherbeat