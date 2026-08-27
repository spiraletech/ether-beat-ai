#include "etherbeat/EtherArrangement.hpp"

// V0.2Q is additive: embed the proven V0.2P Sections shell and add a
// non-destructive arrangement-plan rail above its section/timeline workspace.
#define ETHERBEAT_SECTIONS_EMBEDDED
#include "EtherBeatTabbedWinSections.cpp"
#undef ETHERBEAT_SECTIONS_EMBEDDED

namespace {

std::optional<etherbeat::ArrangementPlan> g_arrangementPlan;
fs::path g_arrangementAudio;
std::size_t g_arrangementIndex = 0;

RectF arrangementCardRect(HWND hwnd) {
    const RectF sections = sectionsRowRect(hwnd);
    return R(sections.X, sections.Y - 68.f, sections.Width, 62.f);
}

std::array<RectF, 7> arrangementButtonRects(HWND hwnd) {
    const RectF card = arrangementCardRect(hwnd);
    constexpr float gap = 4.f;
    constexpr float labelW = 168.f;
    const float buttonW = (card.Width - labelW - gap * 7.f) / 7.f;
    std::array<RectF, 7> result{};
    float x = card.X + labelW + gap;
    for (auto& rect : result) {
        rect = R(x, card.Y + 30.f, buttonW, 25.f);
        x += buttonW + gap;
    }
    return result;
}

void saveArrangementPlan() {
    if (!g_arrangementPlan || g_nowPlaying.empty()) return;
    static_cast<void>(etherbeat::save_arrangement_plan(
        *g_arrangementPlan,
        etherbeat::ether_arrangement_sidecar_path(g_nowPlaying)));
}

void refreshArrangementPlan() {
    if (g_nowPlaying.empty() || !g_nowAnalysis.ready) {
        g_arrangementPlan.reset();
        g_arrangementAudio.clear();
        g_arrangementIndex = 0;
        return;
    }
    if (g_arrangementPlan && g_arrangementAudio == g_nowPlaying) return;

    g_arrangementAudio = g_nowPlaying;
    g_arrangementIndex = 0;
    g_arrangementPlan = etherbeat::load_arrangement_plan_for_audio(g_nowPlaying);
    if (!g_arrangementPlan) {
        refreshSectionMap();
        if (g_sectionMap) {
            g_arrangementPlan = etherbeat::make_arrangement_plan(*g_sectionMap);
            saveArrangementPlan();
        }
    }
    if (g_arrangementPlan && !g_arrangementPlan->slots.empty()) {
        g_arrangementIndex = std::min(g_arrangementIndex, g_arrangementPlan->slots.size() - 1u);
    }
}

void syncTimelineToArrangementSlot() {
    refreshArrangementPlan();
    if (!g_arrangementPlan || g_arrangementPlan->slots.empty()) return;
    g_arrangementIndex = std::min(g_arrangementIndex, g_arrangementPlan->slots.size() - 1u);
    const auto& slot = g_arrangementPlan->slots[g_arrangementIndex];
    if (!slot.has_source_audio() || !g_nowAnalysis.ready) return;
    etherbeat::TimelineSelection selection{
        slot.source_start_seconds,
        slot.source_end_seconds,
        g_nowAnalysis.duration_seconds};
    if (selection.valid()) writeTimelineSelection(selection);
}

std::wstring arrangementSequence() {
    if (!g_arrangementPlan) return L"";
    std::wstring out;
    const std::size_t maxVisible = 8;
    for (std::size_t i = 0; i < g_arrangementPlan->slots.size() && i < maxVisible; ++i) {
        if (i) out += L" > ";
        if (i == g_arrangementIndex) out += L"[";
        out += wide(g_arrangementPlan->slots[i].label);
        if (g_arrangementPlan->slots[i].origin == etherbeat::ArrangementOrigin::Duplicate) out += L"*";
        if (g_arrangementPlan->slots[i].origin == etherbeat::ArrangementOrigin::Placeholder) out += L"+";
        if (i == g_arrangementIndex) out += L"]";
    }
    if (g_arrangementPlan->slots.size() > maxVisible) out += L" > ...";
    return out;
}

void arrangementSelect(int direction) {
    refreshArrangementPlan();
    if (!g_arrangementPlan || g_arrangementPlan->slots.empty()) return;
    const std::size_t size = g_arrangementPlan->slots.size();
    if (direction < 0) g_arrangementIndex = (g_arrangementIndex + size - 1u) % size;
    else g_arrangementIndex = (g_arrangementIndex + 1u) % size;
    syncTimelineToArrangementSlot();
    const auto& slot = g_arrangementPlan->slots[g_arrangementIndex];
    setStatus(L"ETHERARRANGEMENT // slot " + std::to_wstring(g_arrangementIndex + 1u) + L"/" +
              std::to_wstring(size) + L" // " + wide(slot.label));
    InvalidateRect(g_window, nullptr, FALSE);
}

void arrangementDuplicate() {
    refreshArrangementPlan();
    if (!g_arrangementPlan || g_arrangementPlan->slots.empty()) return;
    if (!etherbeat::duplicate_arrangement_slot(*g_arrangementPlan, g_arrangementIndex, true)) return;
    ++g_arrangementIndex;
    saveArrangementPlan();
    syncTimelineToArrangementSlot();
    setStatus(L"ETHERARRANGEMENT // duplicated slot // source audio preserved");
    InvalidateRect(g_window, nullptr, FALSE);
}

void arrangementMove(int direction) {
    refreshArrangementPlan();
    if (!g_arrangementPlan || g_arrangementPlan->slots.empty()) return;
    if (direction < 0) {
        if (g_arrangementIndex == 0) {
            setStatus(L"ETHERARRANGEMENT // already first slot");
            return;
        }
        if (etherbeat::move_arrangement_slot(*g_arrangementPlan, g_arrangementIndex, g_arrangementIndex - 1u)) --g_arrangementIndex;
    } else {
        if (g_arrangementIndex + 1u >= g_arrangementPlan->slots.size()) {
            setStatus(L"ETHERARRANGEMENT // already last slot");
            return;
        }
        if (etherbeat::move_arrangement_slot(*g_arrangementPlan, g_arrangementIndex, g_arrangementIndex + 1u)) ++g_arrangementIndex;
    }
    saveArrangementPlan();
    setStatus(L"ETHERARRANGEMENT // reordered plan // revision " + std::to_wstring(g_arrangementPlan->revision));
    InvalidateRect(g_window, nullptr, FALSE);
}

void arrangementInsertAlternate() {
    refreshArrangementPlan();
    if (!g_arrangementPlan || g_arrangementPlan->slots.empty()) return;
    const auto kind = g_arrangementPlan->slots[g_arrangementIndex].kind;
    const std::string kindName = etherbeat::section_kind_name(kind);
    const std::size_t insertAt = g_arrangementIndex + 1u;
    if (!etherbeat::insert_arrangement_placeholder(
            *g_arrangementPlan,
            insertAt,
            kind,
            kindName + " ALT",
            "generate alternate " + kindName + " section")) return;
    g_arrangementIndex = insertAt;
    saveArrangementPlan();
    setStatus(L"ETHERARRANGEMENT // inserted generated placeholder // " + wide(kindName) + L" ALT");
    InvalidateRect(g_window, nullptr, FALSE);
}

void arrangementErase() {
    refreshArrangementPlan();
    if (!g_arrangementPlan || g_arrangementPlan->slots.empty()) return;
    if (!etherbeat::erase_arrangement_slot(*g_arrangementPlan, g_arrangementIndex)) {
        setStatus(L"ETHERARRANGEMENT // cannot remove the final slot");
        return;
    }
    if (g_arrangementIndex >= g_arrangementPlan->slots.size()) g_arrangementIndex = g_arrangementPlan->slots.size() - 1u;
    saveArrangementPlan();
    syncTimelineToArrangementSlot();
    setStatus(L"ETHERARRANGEMENT // removed slot from plan // source WAV unchanged");
    InvalidateRect(g_window, nullptr, FALSE);
}

void drawArrangementOverlay(HWND hwnd) {
    if (g_screen != Screen::NowPlaying || g_nowPlaying.empty() || !g_nowAnalysis.ready) return;
    refreshArrangementPlan();
    if (!g_arrangementPlan || g_arrangementPlan->slots.empty()) return;

    HDC dc = GetDC(hwnd);
    if (!dc) return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    const RectF card = arrangementCardRect(hwnd);
    roundRect(g, card, 13.f, Color(250, 6, 6, 7), Color(110, 104, 83, 53), 1.f);

    const auto& slot = g_arrangementPlan->slots[g_arrangementIndex];
    const std::wstring title = L"ETHERARRANGEMENT // REV " + std::to_wstring(g_arrangementPlan->revision) +
        L" // " + std::to_wstring(g_arrangementIndex + 1u) + L"/" + std::to_wstring(g_arrangementPlan->slots.size()) +
        L" // " + wide(slot.label) + L" // " + wide(etherbeat::arrangement_origin_name(slot.origin));
    drawText(g, title, R(card.X + 10.f, card.Y + 5.f, card.Width - 20.f, 16.f), 8.f, amber(), FontStyleBold);
    drawText(g, arrangementSequence(), R(card.X + 10.f, card.Y + 18.f, card.Width - 20.f, 13.f), 7.f, muted(), FontStyleBold);

    const auto buttons = arrangementButtonRects(hwnd);
    const std::array<const wchar_t*, 7> labels{L"< SLOT", L"SLOT >", L"DUP", L"< MOVE", L"MOVE >", L"+ ALT", L"DELETE"};
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        roundRect(g, buttons[i], 9.f, Color(245, 11, 11, 12), Color(78, 99, 88, 65), 1.f);
        drawText(g, labels[i], buttons[i], 7.f, (i == 2 || i == 5) ? warm() : muted(), FontStyleBold,
                 StringAlignmentCenter, StringAlignmentCenter);
    }

    ReleaseDC(hwnd, dc);
}

bool handleArrangementClick(HWND hwnd, int x, int y) {
    if (g_screen != Screen::NowPlaying || !g_nowAnalysis.ready) return false;
    refreshArrangementPlan();
    if (!g_arrangementPlan) return false;
    const auto buttons = arrangementButtonRects(hwnd);
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        if (!pointInRect(buttons[i], x, y)) continue;
        switch (i) {
        case 0: arrangementSelect(-1); break;
        case 1: arrangementSelect(1); break;
        case 2: arrangementDuplicate(); break;
        case 3: arrangementMove(-1); break;
        case 4: arrangementMove(1); break;
        case 5: arrangementInsertAlternate(); break;
        case 6: arrangementErase(); break;
        default: break;
        }
        return true;
    }
    return false;
}

LRESULT CALLBACK arrangementWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONUP: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (!g_timelineDragging && handleArrangementClick(hwnd, x, y)) return 0;
        return sectionsWindowProc(hwnd, msg, wParam, lParam);
    }
    case WM_PAINT: {
        const LRESULT result = sectionsWindowProc(hwnd, msg, wParam, lParam);
        drawArrangementOverlay(hwnd);
        return result;
    }
    default:
        break;
    }
    return sectionsWindowProc(hwnd, msg, wParam, lParam);
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
    wc.lpfnWndProc = arrangementWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(
        0, kWindowClass, L"ETHERBEAT // Alien Workshop V0.2Q",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 1000,
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
