#include "etherbeat/EtherSections.hpp"

// V0.2P is additive: reuse the proven V0.2O timeline shell and wrap only the
// window procedure/main entry point with section-map visualization + snapping.
#define timelineWindowProc etherbeat_timeline_legacy_proc
#define wWinMain etherbeat_timeline_legacy_main
#include "EtherBeatTabbedWinTimeline.cpp"
#undef wWinMain
#undef timelineWindowProc

namespace {

std::optional<etherbeat::SectionMap> g_sectionMap;
fs::path g_sectionMapAudio;
bool g_sectionSnap = false;

RectF sectionsRowRect(HWND hwnd) {
    const RectF timeline = timelineCardRect(hwnd);
    return R(timeline.X, timeline.Y - 31.f, timeline.Width, 27.f);
}

void refreshSectionMap() {
    if (g_nowPlaying.empty() || !g_nowAnalysis.ready) {
        g_sectionMap.reset();
        g_sectionMapAudio.clear();
        return;
    }
    if (g_sectionMap && g_sectionMapAudio == g_nowPlaying) return;

    g_sectionMapAudio = g_nowPlaying;
    g_sectionMap = etherbeat::load_section_map_for_audio(g_nowPlaying);
    if (!g_sectionMap) {
        auto inferred = etherbeat::infer_sections(g_nowPlaying, g_nowAnalysis);
        if (!inferred.sections.empty()) {
            static_cast<void>(etherbeat::save_section_map(
                inferred, etherbeat::ether_sections_sidecar_path(g_nowPlaying)));
            g_sectionMap = std::move(inferred);
        }
    }
}

std::array<RectF, 6> sectionButtonRects(HWND hwnd) {
    const RectF row = sectionsRowRect(hwnd);
    constexpr float gap = 5.f;
    const float snapW = 74.f;
    const float sectionW = (row.Width - snapW - gap * 5.f) / 5.f;
    std::array<RectF, 6> result{};
    float x = row.X;
    for (std::size_t i = 0; i < 5; ++i) {
        result[i] = R(x, row.Y, sectionW, row.Height);
        x += sectionW + gap;
    }
    result[5] = R(row.GetRight() - snapW, row.Y, snapW, row.Height);
    return result;
}

std::wstring sectionTime(double seconds) {
    std::wostringstream out;
    out << std::fixed << std::setprecision(2) << seconds;
    return out.str();
}

void selectNamedSection(etherbeat::SectionKind kind) {
    refreshSectionMap();
    if (!g_sectionMap) {
        setStatus(L"ETHERSECTIONS // no section map available");
        return;
    }
    const auto selection = etherbeat::section_selection(*g_sectionMap, kind);
    if (!selection.valid()) return;
    writeTimelineSelection(selection);
    setStatus(
        L"ETHERSECTIONS // " + wide(etherbeat::section_kind_name(kind)) +
        L" // " + sectionTime(selection.start_seconds) + L" -> " +
        sectionTime(selection.end_seconds));
    InvalidateRect(g_window, nullptr, FALSE);
}

void snapCurrentTimelineSelection() {
    refreshSectionMap();
    if (!g_sectionMap || !g_nowAnalysis.ready) return;
    const double duration = g_nowAnalysis.duration_seconds;
    const double start = parseDouble(g_controlStart, 0.0, 0.0, duration);
    const double end = parseDouble(g_controlEnd, duration, 0.0, duration);
    const auto loose = etherbeat::make_timeline_selection(
        duration,
        etherbeat::timeline_normalized_position(duration, start),
        etherbeat::timeline_normalized_position(duration, end));
    const auto snapped = etherbeat::snap_selection_to_section(*g_sectionMap, loose);
    if (!snapped.valid()) return;
    writeTimelineSelection(snapped);

    std::wstring label = L"SECTION";
    for (const auto& section : g_sectionMap->sections) {
        if (std::abs(section.start_seconds - snapped.start_seconds) < 0.01 &&
            std::abs(section.end_seconds - snapped.end_seconds) < 0.01) {
            label = wide(section.label);
            break;
        }
    }
    setStatus(L"ETHERSECTIONS // snapped to " + label + L" // " +
              sectionTime(snapped.start_seconds) + L" -> " + sectionTime(snapped.end_seconds));
}

void drawSectionsOverlay(HWND hwnd) {
    if (g_screen != Screen::NowPlaying || g_nowPlaying.empty() || !g_nowAnalysis.ready) return;
    refreshSectionMap();
    if (!g_sectionMap) return;

    HDC dc = GetDC(hwnd);
    if (!dc) return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    const auto buttons = sectionButtonRects(hwnd);
    for (std::size_t i = 0; i < 5 && i < g_sectionMap->sections.size(); ++i) {
        const auto& section = g_sectionMap->sections[i];
        roundRect(g, buttons[i], 10.f, Color(245, 10, 10, 11), Color(80, 103, 91, 61), 1.f);
        drawText(g, wide(section.label), buttons[i], 8.f, warm(), FontStyleBold,
                 StringAlignmentCenter, StringAlignmentCenter);
    }
    roundRect(g, buttons[5], 10.f,
              g_sectionSnap ? Color(255, 34, 29, 10) : Color(245, 10, 10, 11),
              g_sectionSnap ? amber(190) : Color(80, 103, 91, 61), 1.f);
    drawText(g, g_sectionSnap ? L"SNAP ON" : L"SNAP", buttons[5], 8.f,
             g_sectionSnap ? amber() : muted(), FontStyleBold,
             StringAlignmentCenter, StringAlignmentCenter);

    // Draw structural boundaries over the real waveform without obscuring it.
    const RectF track = timelineTrackRect(hwnd);
    const double duration = std::max(0.001, g_sectionMap->duration_seconds);
    Pen boundary(Color(125, 242, 195, 61), 1.f);
    for (std::size_t i = 1; i < g_sectionMap->sections.size(); ++i) {
        const double n = etherbeat::timeline_normalized_position(
            duration, g_sectionMap->sections[i].start_seconds);
        const float x = track.X + static_cast<float>(n) * track.Width;
        g.DrawLine(&boundary, x, track.Y, x, track.GetBottom());
    }

    ReleaseDC(hwnd, dc);
}

bool handleSectionClick(HWND hwnd, int x, int y) {
    if (g_screen != Screen::NowPlaying || !g_nowAnalysis.ready) return false;
    refreshSectionMap();
    if (!g_sectionMap) return false;
    const auto buttons = sectionButtonRects(hwnd);
    const std::array<etherbeat::SectionKind, 5> kinds{
        etherbeat::SectionKind::Intro,
        etherbeat::SectionKind::Verse,
        etherbeat::SectionKind::Hook,
        etherbeat::SectionKind::Bridge,
        etherbeat::SectionKind::Outro};
    for (std::size_t i = 0; i < 5; ++i) {
        if (pointInRect(buttons[i], x, y)) {
            selectNamedSection(kinds[i]);
            return true;
        }
    }
    if (pointInRect(buttons[5], x, y)) {
        g_sectionSnap = !g_sectionSnap;
        setStatus(g_sectionSnap
            ? L"ETHERSECTIONS // timeline section snapping enabled"
            : L"ETHERSECTIONS // free timeline selection enabled");
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    return false;
}

LRESULT CALLBACK sectionsWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONUP: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (!g_timelineDragging && handleSectionClick(hwnd, x, y)) return 0;
        const bool wasTimelineDrag = g_timelineDragging;
        const LRESULT result = etherbeat_timeline_legacy_proc(hwnd, msg, wParam, lParam);
        if (wasTimelineDrag && g_sectionSnap) {
            snapCurrentTimelineSelection();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return result;
    }
    case WM_PAINT: {
        const LRESULT result = etherbeat_timeline_legacy_proc(hwnd, msg, wParam, lParam);
        drawSectionsOverlay(hwnd);
        return result;
    }
    default:
        break;
    }
    return etherbeat_timeline_legacy_proc(hwnd, msg, wParam, lParam);
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
    wc.lpfnWndProc = sectionsWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(
        0, kWindowClass, L"ETHERBEAT // Alien Workshop V0.2P",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 940,
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
