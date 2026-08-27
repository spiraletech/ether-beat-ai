#include "etherbeat/EtherTimeline.hpp"

// Reuse the proven V0.2N shell as the UI anchor while wrapping its native
// window procedure with timeline painting and drag-selection behavior.
#define windowProc etherbeat_compare_legacy_proc
#define wWinMain etherbeat_compare_legacy_main
#include "EtherBeatTabbedWinCompare.cpp"
#undef wWinMain
#undef windowProc

namespace {

constexpr UINT_PTR kTimelineTimer = 0xE7A1;
bool g_timelineDragging = false;
double g_timelineAnchor = 0.0;

RectF timelineCardRect(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const float W = static_cast<float>(client.right - client.left);
    const float H = static_cast<float>(client.bottom - client.top);
    const float contentW = W - 84.f;
    const float analyzerW = contentW * .58f;
    return R(62.f, H - 386.f, std::max(280.f, analyzerW - 40.f), 108.f);
}

RectF timelineTrackRect(HWND hwnd) {
    const RectF card = timelineCardRect(hwnd);
    return R(card.X + 12.f, card.Y + 34.f, card.Width - 24.f, card.Height - 50.f);
}

bool pointInRect(const RectF& r, int x, int y) {
    return static_cast<float>(x) >= r.X && static_cast<float>(x) <= r.GetRight() &&
           static_cast<float>(y) >= r.Y && static_cast<float>(y) <= r.GetBottom();
}

double timelineNormalizedFromMouse(HWND hwnd, int x) {
    const RectF track = timelineTrackRect(hwnd);
    if (track.Width <= 0.f) return 0.0;
    return std::clamp(
        (static_cast<double>(x) - static_cast<double>(track.X)) /
            static_cast<double>(track.Width),
        0.0,
        1.0);
}

std::wstring timelineTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const int minutes = static_cast<int>(seconds / 60.0);
    const double remaining = seconds - static_cast<double>(minutes) * 60.0;
    std::wostringstream out;
    out << minutes << L":" << std::setfill(L'0') << std::setw(5)
        << std::fixed << std::setprecision(2) << remaining;
    return out.str();
}

void writeTimelineSelection(const etherbeat::TimelineSelection& selection) {
    if (!selection.valid() || !g_controlStart || !g_controlEnd) return;
    std::wostringstream start;
    start << std::fixed << std::setprecision(2) << selection.start_seconds;
    std::wostringstream end;
    end << std::fixed << std::setprecision(2) << selection.end_seconds;
    SetWindowTextW(g_controlStart, start.str().c_str());
    SetWindowTextW(g_controlEnd, end.str().c_str());
}

void applyTimelineDrag(HWND hwnd, double normalizedCurrent, bool announce) {
    if (!g_nowAnalysis.ready || g_nowAnalysis.duration_seconds <= 0.0) return;
    const auto selection = etherbeat::make_timeline_selection(
        g_nowAnalysis.duration_seconds,
        g_timelineAnchor,
        normalizedCurrent,
        0.25);
    writeTimelineSelection(selection);
    if (announce) {
        setStatus(
            L"ETHERTIMELINE // repaint selection " +
            timelineTime(selection.start_seconds) + L" -> " +
            timelineTime(selection.end_seconds));
    }
    RECT invalid{};
    const auto card = timelineCardRect(hwnd);
    invalid.left = static_cast<LONG>(card.X - 2.f);
    invalid.top = static_cast<LONG>(card.Y - 2.f);
    invalid.right = static_cast<LONG>(card.GetRight() + 2.f);
    invalid.bottom = static_cast<LONG>(card.GetBottom() + 2.f);
    InvalidateRect(hwnd, &invalid, FALSE);
}

void drawTimelineOverlay(HWND hwnd) {
    if (g_screen != Screen::NowPlaying || g_nowPlaying.empty() || !g_nowAnalysis.ready) return;

    HDC dc = GetDC(hwnd);
    if (!dc) return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    const RectF card = timelineCardRect(hwnd);
    const RectF track = timelineTrackRect(hwnd);
    roundRect(g, card, 14.f, Color(252, 5, 5, 6), Color(125, 92, 75, 45), 1.f);

    drawText(g, L"ETHERTIMELINE // DRAG TO SELECT REPAINT REGION",
             R(card.X + 10.f, card.Y + 7.f, card.Width - 20.f, 18.f),
             9.f, amber(), FontStyleBold);

    const double duration = std::max(0.001, g_nowAnalysis.duration_seconds);
    const double startSeconds = parseDouble(g_controlStart, 0.0, 0.0, duration);
    const double endSeconds = parseDouble(g_controlEnd, std::min(8.0, duration), 0.0, duration);
    const double startNorm = etherbeat::timeline_normalized_position(duration, std::min(startSeconds, endSeconds));
    const double endNorm = etherbeat::timeline_normalized_position(duration, std::max(startSeconds, endSeconds));

    SolidBrush trackBackground(Color(255, 10, 10, 11));
    g.FillRectangle(&trackBackground, track);

    const float center = track.Y + track.Height * .5f;
    Pen centerLine(Color(70, 118, 108, 88), 1.f);
    g.DrawLine(&centerLine, track.X, center, track.GetRight(), center);

    const float binWidth = track.Width / static_cast<float>(g_nowAnalysis.timeline_envelope.size());
    SolidBrush waveform(Color(205, 151, 88, 236));
    for (std::size_t i = 0; i < g_nowAnalysis.timeline_envelope.size(); ++i) {
        const float value = std::clamp(g_nowAnalysis.timeline_envelope[i], 0.0f, 1.0f);
        const float halfHeight = std::max(1.0f, value * track.Height * .45f);
        const float x = track.X + static_cast<float>(i) * binWidth;
        g.FillRectangle(&waveform, x, center - halfHeight, std::max(1.f, binWidth * .72f), halfHeight * 2.f);
    }

    const float selectionX = track.X + static_cast<float>(startNorm) * track.Width;
    const float selectionRight = track.X + static_cast<float>(endNorm) * track.Width;
    SolidBrush selection(Color(68, 242, 195, 61));
    g.FillRectangle(&selection, selectionX, track.Y, std::max(2.f, selectionRight - selectionX), track.Height);
    Pen selectionEdge(amber(225), 1.5f);
    g.DrawLine(&selectionEdge, selectionX, track.Y, selectionX, track.GetBottom());
    g.DrawLine(&selectionEdge, selectionRight, track.Y, selectionRight, track.GetBottom());

    const unsigned long positionMs = playbackPositionMs();
    const double playheadNorm = etherbeat::timeline_normalized_position(
        duration, static_cast<double>(positionMs) / 1000.0);
    const float playheadX = track.X + static_cast<float>(playheadNorm) * track.Width;
    Pen playhead(Color(235, 244, 242, 235), 1.f);
    g.DrawLine(&playhead, playheadX, track.Y, playheadX, track.GetBottom());

    const std::wstring label =
        timelineTime(std::min(startSeconds, endSeconds)) + L" -> " +
        timelineTime(std::max(startSeconds, endSeconds)) +
        (g_compareAuditionB ? L" // B AUDITIONING, EDIT SOURCE REMAINS A" : L" // A SOURCE");
    drawText(g, label,
             R(card.X + 10.f, card.GetBottom() - 16.f, card.Width - 20.f, 13.f),
             8.f, muted(), FontStyleBold);

    ReleaseDC(hwnd, dc);
}

LRESULT CALLBACK timelineWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        const LRESULT result = etherbeat_compare_legacy_proc(hwnd, msg, wParam, lParam);
        SetTimer(hwnd, kTimelineTimer, 100, nullptr);
        return result;
    }
    case WM_TIMER:
        if (wParam == kTimelineTimer && g_screen == Screen::NowPlaying && !g_nowPlaying.empty()) {
            const auto card = timelineCardRect(hwnd);
            RECT invalid{
                static_cast<LONG>(card.X - 2.f),
                static_cast<LONG>(card.Y - 2.f),
                static_cast<LONG>(card.GetRight() + 2.f),
                static_cast<LONG>(card.GetBottom() + 2.f)};
            InvalidateRect(hwnd, &invalid, FALSE);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN: {
        if (g_screen == Screen::NowPlaying && g_nowAnalysis.ready) {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const RectF track = timelineTrackRect(hwnd);
            if (pointInRect(track, x, y)) {
                g_timelineDragging = true;
                g_timelineAnchor = timelineNormalizedFromMouse(hwnd, x);
                SetCapture(hwnd);
                applyTimelineDrag(hwnd, g_timelineAnchor, false);
                return 0;
            }
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (g_timelineDragging) {
            applyTimelineDrag(hwnd, timelineNormalizedFromMouse(hwnd, GET_X_LPARAM(lParam)), false);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_timelineDragging) {
            applyTimelineDrag(hwnd, timelineNormalizedFromMouse(hwnd, GET_X_LPARAM(lParam)), true);
            g_timelineDragging = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        g_timelineDragging = false;
        break;
    case WM_PAINT: {
        const LRESULT result = etherbeat_compare_legacy_proc(hwnd, msg, wParam, lParam);
        drawTimelineOverlay(hwnd);
        return result;
    }
    case WM_DESTROY:
        KillTimer(hwnd, kTimelineTimer);
        return etherbeat_compare_legacy_proc(hwnd, msg, wParam, lParam);
    default:
        break;
    }
    return etherbeat_compare_legacy_proc(hwnd, msg, wParam, lParam);
}

} // namespace

#ifndef ETHERBEAT_TIMELINE_EMBEDDED
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
    wc.lpfnWndProc = timelineWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(
        0, kWindowClass, L"ETHERBEAT // Alien Workshop V0.2O",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 920,
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
#endif
