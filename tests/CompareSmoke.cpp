#include "etherbeat/EtherCompare.hpp"
#include "etherbeat/EtherDNA.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(float a, float b, float tolerance = 0.0001f) {
    return std::fabs(a - b) <= tolerance;
}

etherbeat::EtherDNA make_dna(const std::filesystem::path& path, float shift) {
    etherbeat::EtherDNA dna;
    dna.source_audio = path;
    dna.energy = 0.40f + shift;
    dna.bass = 0.55f + shift;
    dna.mid = 0.48f - shift * 0.5f;
    dna.treble = 0.30f + shift;
    dna.beat_peak = 0.35f + shift;
    dna.low_end_weight = 0.62f + shift;
    dna.brightness = 0.28f + shift;
    dna.darkness = 0.72f - shift;
    dna.rhythmic_activity = 0.38f + shift;
    dna.spectral_center = 0.36f + shift;
    for (std::size_t i = 0; i < dna.spectrum.size(); ++i) {
        dna.spectrum[i] = 0.10f + static_cast<float>(i) * 0.01f + shift;
    }
    return dna;
}

} // namespace

int main() {
    try {
        const auto a = make_dna("a.wav", 0.0f);
        const auto same = make_dna("same.wav", 0.0f);
        const auto b = make_dna("b.wav", 0.12f);

        const auto identical = etherbeat::compare_ether_dna(a, same);
        require(near(identical.similarity, 1.0f), "identical DNA should score 1.0 similarity");
        require(near(identical.delta.spectrum_rmse, 0.0f), "identical spectrum RMSE should be zero");

        const auto changed = etherbeat::compare_ether_dna(a, b);
        require(changed.similarity < 1.0f && changed.similarity > 0.0f,
                "changed DNA similarity must remain normalized");
        require(changed.delta.energy > 0.0f, "energy delta sign is wrong");
        require(changed.delta.low_end_weight > 0.0f, "low-end delta sign is wrong");
        require(changed.delta.brightness > 0.0f, "brightness delta sign is wrong");
        require(changed.delta.darkness < 0.0f, "darkness delta sign is wrong");
        require(changed.delta.rhythmic_activity > 0.0f, "rhythmic delta sign is wrong");
        require(changed.delta.spectrum_rmse > 0.0f, "spectrum RMSE should detect change");

        std::cout << "EtherCompare smoke passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "EtherCompare smoke failed: " << e.what() << '\n';
        return 1;
    }
}
