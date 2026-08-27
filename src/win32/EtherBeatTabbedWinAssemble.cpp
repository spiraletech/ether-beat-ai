#include "etherbeat/AudioDecodeWin.hpp"
#include "etherbeat/EtherAssemble.hpp"

#include <chrono>

// V0.2R is additive: keep the proven V0.2Q arrangement workspace intact and
// wrap it with one physical render action that turns the graph into a WAV.
#define arrangementWindowProc etherbeat_arrangement_legacy_proc
#define wWinMain etherbeat_arrangement_legacy_main
#include "EtherBeatTabbedWinArrangement.cpp"
#undef wWinMain
#undef arrangementWindowProc

namespace {

constexpr UINT WM_APP_ASSEMBLE_DONE = WM_APP + 97;
std::mutex g_assembleMutex;
bool g_assembleSuccess = false;
std::wstring g_assembleError;
fs::path g_assembleArtifact;
etherbeat::AudioAnalysis g_assembleAnalysis{};
std::size_t g_assembleSlotCount = 0;

RectF assembleCardRect(HWND hwnd) {
    const RectF arrangement = arrangementCardRect(hwnd);
    return R(arrangement.X, arrangement.Y - 42.f, arrangement.Width, 36.f);
}

RectF assembleButtonRect(HWND hwnd) {
    const RectF card = assembleCardRect(hwnd);
    return R(card.GetRight() - 150.f, card.Y + 5.f, 142.f, 26.f);
}

std::filesystem::path makeAssembledOutput(const fs::path& source, std::uint64_t revision) {
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::error_code ec;
    const auto root = libraryRoot() / L"assembled" / source.stem();
    fs::create_directories(root, ec);
    return root / (source.stem().wstring() + L"-arranged-r" + std::to_wstring(revision) +
                   L"-" + std::to_wstring(millis) + L".wav");
}

void postAssembleResult(bool success, std::wstring error, fs::path artifact,
                        etherbeat::AudioAnalysis analysis, std::size_t slot_count) {
    {
        std::scoped_lock lock(g_assembleMutex);
        g_assembleSuccess = success;
        g_assembleError = std::move(error);
        g_assembleArtifact = std::move(artifact);
        g_assembleAnalysis = std::move(analysis);
        g_assembleSlotCount = slot_count;
    }
    if (g_window) PostMessageW(g_window, WM_APP_ASSEMBLE_DONE, 0, 0);
}

void startAssemble() {
    if (g_working.load()) return;
    refreshArrangementPlan();
    if (!g_arrangementPlan || g_arrangementPlan->slots.empty() || g_nowPlaying.empty()) {
        setStatus(L"ETHERASSEMBLE // no arrangement plan available");
        return;
    }

    const auto plan = *g_arrangementPlan;
    const auto source = g_nowPlaying;
    const auto output = makeAssembledOutput(source, plan.revision);
    const auto root = libraryRoot();
    const double bpm = parseDouble(g_bpm, 0.0, 0.0, 300.0);
    const std::string key = utf8(getText(g_key));
    const std::string producer_context = utf8(getText(g_controlPrompt));
    const std::string blueprint = etherbeat::arrangement_blueprint(plan);

    if (g_working.exchange(true)) return;
    stopPlayback();
    setStatus(L"ETHERASSEMBLE // resolving slots, generating placeholders, crossfading WAV...");
    InvalidateRect(g_window, nullptr, FALSE);

    std::thread([plan, source, output, root, bpm, key, producer_context, blueprint] {
        try {
            auto router = etherbeat::make_default_router();
            const auto fragment_root = output.parent_path() / L"fragments";
            std::error_code ec;
            fs::create_directories(fragment_root, ec);

            etherbeat::EtherAssemble assembler;
            const auto rendered = assembler.render(
                plan,
                output,
                [](const fs::path& path) {
                    return etherbeat::decode_audio_pcm_file(path);
                },
                [&](const etherbeat::ArrangementSlot& slot, double expected_duration)
                    -> std::optional<etherbeat::PcmAudio> {
                    etherbeat::GenerationRequest request;
                    request.mode = etherbeat::GenerationMode::TextToInstrumental;
                    request.render_intent = etherbeat::RenderIntent::Quality;
                    request.duration_seconds = std::clamp(expected_duration, 1.0, 120.0);
                    request.bpm = bpm;
                    request.key = key;
                    request.prompt = slot.instruction.empty()
                        ? "Generate one alternate " + slot.label + " section for song assembly."
                        : slot.instruction;
                    request.prompt += " Render only this section as self-contained instrumental material."
                        " It will be inserted into an existing song; avoid intro/outro padding.";
                    if (!producer_context.empty()) request.prompt += " Producer direction: " + producer_context;
                    request.prompt += " Arrangement blueprint: " + blueprint;

                    const auto artifact = router.generate(request, fragment_root);
                    return etherbeat::decode_audio_pcm_file(artifact.audio_path);
                },
                {.crossfade_seconds=0.020, .require_all_placeholders=true});

            const auto analysis = etherbeat::analyze_audio_file(rendered.audio_path);
            if (!analysis.ready) {
                throw std::runtime_error(analysis.error.empty()
                    ? "assembled WAV was written but analysis failed"
                    : analysis.error);
            }
            const auto dna = etherbeat::make_ether_dna(rendered.audio_path, analysis);
            if (!etherbeat::save_ether_dna(dna, etherbeat::ether_dna_sidecar_path(rendered.audio_path))) {
                throw std::runtime_error("assembled WAV analysis succeeded but EtherDNA persistence failed");
            }

            etherbeat::EtherVersions versions(root);
            versions.ensure_root(source);
            versions.register_child(
                source,
                rendered.audio_path,
                "arrangement_assemble",
                "assembled arrangement revision " + std::to_string(plan.revision));

            postAssembleResult(true, L"", rendered.audio_path, analysis, rendered.slots.size());
        } catch (const std::exception& e) {
            postAssembleResult(false, wide(e.what()), {}, {}, 0);
        }
    }).detach();
}

void drawAssembleOverlay(HWND hwnd) {
    if (g_screen != Screen::NowPlaying || g_nowPlaying.empty() || !g_nowAnalysis.ready) return;
    refreshArrangementPlan();
    if (!g_arrangementPlan) return;

    HDC dc = GetDC(hwnd);
    if (!dc) return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    const RectF card = assembleCardRect(hwnd);
    const RectF button = assembleButtonRect(hwnd);
    roundRect(g, card, 12.f, Color(250, 7, 7, 8), Color(105, 105, 85, 53), 1.f);
    drawText(g,
             L"ETHERASSEMBLE // REAL WAV // 20ms XFADE // +ALT = QUALITY GENERATE",
             R(card.X + 10.f, card.Y + 10.f, card.Width - 170.f, 18.f),
             8.f, muted(), FontStyleBold);
    roundRect(g, button, 10.f,
              g_working ? Color(245, 18, 18, 19) : Color(255, 34, 29, 10),
              g_working ? Color(80, 90, 85, 70) : amber(190), 1.f);
    drawText(g, g_working ? L"WORKING..." : L"ASSEMBLE SONG", button, 8.f,
             g_working ? muted() : amber(), FontStyleBold,
             StringAlignmentCenter, StringAlignmentCenter);
    ReleaseDC(hwnd, dc);
}

LRESULT CALLBACK assembleWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONUP: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (g_screen == Screen::NowPlaying && pointInRect(assembleButtonRect(hwnd), x, y)) {
            startAssemble();
            return 0;
        }
        return etherbeat_arrangement_legacy_proc(hwnd, msg, wParam, lParam);
    }
    case WM_APP_ASSEMBLE_DONE: {
        bool success = false;
        std::wstring error;
        fs::path artifact;
        etherbeat::AudioAnalysis analysis;
        std::size_t slots = 0;
        {
            std::scoped_lock lock(g_assembleMutex);
            success = g_assembleSuccess;
            error = g_assembleError;
            artifact = g_assembleArtifact;
            analysis = g_assembleAnalysis;
            slots = g_assembleSlotCount;
        }
        g_working = false;
        if (success && !artifact.empty()) {
            refreshLibrary();
            loadNowPlaying(artifact, true);
            setStatus(L"ETHERASSEMBLE // COMPLETE // " + std::to_wstring(slots) +
                      L" slots // " + std::to_wstring(static_cast<int>(analysis.duration_seconds)) + L" sec // new version child");
        } else {
            setStatus(L"ETHERASSEMBLE // FAILED // " + error);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_PAINT: {
        const LRESULT result = etherbeat_arrangement_legacy_proc(hwnd, msg, wParam, lParam);
        drawAssembleOverlay(hwnd);
        return result;
    }
    default:
        break;
    }
    return etherbeat_arrangement_legacy_proc(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_instance = instance;

    GdiplusStartupInput input;
    if (GdiplusStartup(&g_gdiplus, &input, nullptr) != Ok) return 1;

    g_uiFont = CreateFontW(-16, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_promptFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_editBrush = CreateSolidBrush(RGB(12, 12, 13));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = assembleWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(
        0, kWindowClass, L"ETHERBEAT // Alien Workshop V0.2R",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 1040,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, kImmersiveDarkModeAttribute, &dark, sizeof(dark));
    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_editBrush) DeleteObject(g_editBrush);
    if (g_uiFont) DeleteObject(g_uiFont);
    if (g_promptFont) DeleteObject(g_promptFont);
    GdiplusShutdown(g_gdiplus);
    return static_cast<int>(message.wParam);
}
