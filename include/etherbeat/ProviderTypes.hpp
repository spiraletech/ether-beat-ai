#pragma once

#include <cstdint>
#include <string_view>

namespace etherbeat {

enum class RenderIntent {
    Auto,
    Draft,
    Quality,
    Control,
    Vocal
};

enum class ProviderCapability : std::uint32_t {
    None = 0,

    TextToInstrumental = 1u << 0,
    Variation          = 1u << 1,
    Extend             = 1u << 2,
    AudioToAudio       = 1u << 3,
    ReferenceAudio     = 1u << 4,

    DraftRole          = 1u << 8,
    QualityRole        = 1u << 9,
    ControlRole        = 1u << 10,
    VocalRole          = 1u << 11,

    LocalRuntime       = 1u << 16
};

using ProviderCapabilities = std::uint32_t;

[[nodiscard]] constexpr ProviderCapabilities capability(ProviderCapability value) noexcept {
    return static_cast<ProviderCapabilities>(value);
}

[[nodiscard]] constexpr ProviderCapabilities operator|(ProviderCapability a, ProviderCapability b) noexcept {
    return capability(a) | capability(b);
}

[[nodiscard]] constexpr ProviderCapabilities operator|(ProviderCapabilities a, ProviderCapability b) noexcept {
    return a | capability(b);
}

[[nodiscard]] constexpr bool has_capability(ProviderCapabilities value, ProviderCapability flag) noexcept {
    return (value & capability(flag)) != 0;
}

[[nodiscard]] constexpr std::string_view render_intent_name(RenderIntent intent) noexcept {
    switch (intent) {
    case RenderIntent::Auto: return "auto";
    case RenderIntent::Draft: return "draft";
    case RenderIntent::Quality: return "quality";
    case RenderIntent::Control: return "control";
    case RenderIntent::Vocal: return "vocal";
    }
    return "unknown";
}

} // namespace etherbeat
