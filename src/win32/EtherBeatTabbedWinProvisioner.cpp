#include "etherbeat/AceStepEngineManager.hpp"

// V0.2T is intentionally additive over the proven V0.2S producer shell.
// It changes the critical first-run engine bootstrap/diagnostics path only.
#define ETHERBEAT_ASSEMBLE_EMBEDDED
#include "EtherBeatTabbedWinAssemble.cpp"
#undef ETHERBEAT_ASSEMBLE_EMBEDDED

namespace {

constexpr UINT_PTR kProvisionTimer = 0xE73;

Color provisionStateColor(const etherbeat::EngineProvisionStatus& status) {
    using State = etherbeat::EngineProvisionState;
    if (status.state == State::Ready) return Color(255, 176, 228, 156);
    if (status.state == State::Failed) return Color(255, 235, 130, 120);
    switch (status.state) {
    case State::RuntimeDownloading:
    case State::RuntimeExtracting:
    case State::ApiStarting:
    case State::ModelDownloading:
    case State::ModelLoading:
    case State::Warmup:
        return amber();
    default:
        return warm();
    }
}

std::wstring provisionStateText(const etherbeat::EngineProvisionStatus& status) {
    return wide(etherbeat::engine_provision_state_name(status.state));
}

std::wstring yesNo(bool value, const wchar_t* yes, const wchar_t* no) {
    return value ? std::wstring(yes) : std::wstring(no);
}

void drawProgress(Graphics& g, const RectF& rect, const etherbeat::EngineProvisionStatus& status) {
    roundRect(g, rect, 6.f, Color(255, 17, 16, 14), Color(70, 103, 88, 55), 1.f);
    const float progress = static_cast<float>(std::clamp(status.progress, 0.0, 1.0));
    if (progress > 0.f) {
        RectF fill = rect;
        fill.Width *= progress;
        roundRect(g, fill, 6.f, Color(255, 86, 69, 13), amber(160), 1.f);
    }
}

void drawProvisionFacts(Graphics& g, const RectF& area,
                        const etherbeat::EngineProvisionStatus& status) {
    const float x = area.X;
    const float y = area.Y;
    const float col = area.Width * .5f;

    drawText(g, L"RUNTIME", R(x, y, col, 16.f), 8.f, muted(), FontStyleBold);
    drawText(g, yesNo(status.runtime_installed, L"INSTALLED", L"MISSING"),
             R(x, y + 18.f, col, 19.f), 10.f,
             status.runtime_installed ? warm() : Color(255, 235, 130, 120), FontStyleBold);

    drawText(g, L"LOCAL API", R(x + col, y, col, 16.f), 8.f, muted(), FontStyleBold);
    drawText(g, yesNo(status.api_online, L"ONLINE", L"OFFLINE"),
             R(x + col, y + 18.f, col, 19.f), 10.f,
             status.api_online ? warm() : muted(), FontStyleBold);

    drawText(g, L"DIT MODEL", R(x, y + 48.f, col, 16.f), 8.f, muted(), FontStyleBold);
    drawText(g, status.model_loaded ? L"LOADED" :
             (status.model_files_present ? L"FILES PRESENT" : L"NOT READY"),
             R(x, y + 66.f, col, 19.f), 10.f,
             status.model_loaded ? Color(255, 176, 228, 156) : muted(), FontStyleBold);

    drawText(g, L"REAL WARMUP", R(x + col, y + 48.f, col, 16.f), 8.f, muted(), FontStyleBold);
    drawText(g, status.warmup_passed ? L"WAV VERIFIED" : L"PENDING",
             R(x + col, y + 66.f, col, 19.f), 10.f,
             status.warmup_passed ? Color(255, 176, 228, 156) : muted(), FontStyleBold);
}

void drawProvisionerHomeOverlay(HWND hwnd) {
    if (g_screen != Screen::Home) return;
    RECT client{};
    GetClientRect(hwnd, &client);
    const float W = static_cast<float>(client.right);
    const float H = static_cast<float>(client.bottom);
    const RectF left = R(42.f, 226.f, W * .46f, H - 300.f);
    const RectF right = R(left.GetRight() + 18.f, 226.f, W - left.GetRight() - 60.f, H - 300.f);
    const auto status = etherbeat::managed_ace_step_engine_status();

    HDC dc = GetDC(hwnd);
    if (!dc) return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    roundRect(g, right, 24.f, panel(), Color(85, 98, 89, 70));
    drawText(g, L"ENGINE PROVISIONER", R(right.X + 22.f, right.Y + 22.f, right.Width - 44.f, 20.f),
             11.f, amber(), FontStyleBold);
    drawText(g, provisionStateText(status), R(right.X + 22.f, right.Y + 56.f, right.Width - 44.f, 30.f),
             18.f, provisionStateColor(status), FontStyleBold);

    const std::wstring detail = status.error.empty() ? wide(status.detail) : wide(status.error);
    drawText(g, detail.empty() ? L"Press INSTALL / VERIFY to prove the local model." : detail,
             R(right.X + 22.f, right.Y + 94.f, right.Width - 44.f, 48.f),
             10.f, status.error.empty() ? muted() : Color(255, 235, 130, 120), FontStyleRegular);

    drawProgress(g, R(right.X + 22.f, right.Y + 150.f, right.Width - 44.f, 12.f), status);
    drawProvisionFacts(g, R(right.X + 22.f, right.Y + 180.f, right.Width - 44.f, 94.f), status);

    drawText(g, status.ready()
                 ? L"Generation is unlocked because a real self-test WAV completed."
                 : L"READY is impossible until model load + real inference self-test both pass.",
             R(right.X + 22.f, right.Y + 292.f, right.Width - 44.f, 44.f),
             9.f, status.ready() ? Color(255, 176, 228, 156) : muted(), FontStyleBold);

    drawButton(g, R(right.X + 22.f, right.GetBottom() - 62.f, 176.f, 40.f),
               g_working ? L"PROVISIONING..." : (status.ready() ? L"VERIFY AGAIN" : L"INSTALL / VERIFY"),
               ActStartEngine, true);
    ReleaseDC(hwnd, dc);
}

void drawProvisionerEngineOverlay(HWND hwnd) {
    if (g_screen != Screen::Engine) return;
    RECT client{};
    GetClientRect(hwnd, &client);
    const float W = static_cast<float>(client.right);
    const float H = static_cast<float>(client.bottom);
    const RectF card = R(42.f, 216.f, W - 84.f, H - 286.f);
    const float panelX = card.X + card.Width * .57f;
    const RectF diagnostics = R(panelX, card.Y + 18.f, card.GetRight() - panelX - 18.f, card.Height - 94.f);
    const auto status = etherbeat::managed_ace_step_engine_status();

    HDC dc = GetDC(hwnd);
    if (!dc) return;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    roundRect(g, diagnostics, 17.f, Color(255, 9, 9, 9), Color(92, 105, 89, 61), 1.f);
    drawText(g, L"ETHERENGINE // FIRST-RUN ACCEPTANCE", R(diagnostics.X + 18.f, diagnostics.Y + 14.f, diagnostics.Width - 36.f, 20.f),
             9.f, amber(), FontStyleBold);
    drawText(g, provisionStateText(status), R(diagnostics.X + 18.f, diagnostics.Y + 42.f, diagnostics.Width - 36.f, 30.f),
             17.f, provisionStateColor(status), FontStyleBold);

    drawProgress(g, R(diagnostics.X + 18.f, diagnostics.Y + 82.f, diagnostics.Width - 36.f, 12.f), status);
    drawProvisionFacts(g, R(diagnostics.X + 18.f, diagnostics.Y + 116.f, diagnostics.Width - 36.f, 94.f), status);

    const std::wstring detail = status.error.empty() ? wide(status.detail) : wide(status.error);
    drawText(g, detail.empty() ? L"No provisioning attempt has run yet." : detail,
             R(diagnostics.X + 18.f, diagnostics.Y + 226.f, diagnostics.Width - 36.f, 58.f),
             9.f, status.error.empty() ? warm() : Color(255, 235, 130, 120), FontStyleBold);

    drawText(g, L"SERVER LOG", R(diagnostics.X + 18.f, diagnostics.Y + 302.f, diagnostics.Width - 36.f, 16.f),
             8.f, muted(), FontStyleBold);
    drawText(g, etherbeat::managed_ace_step_log_path().wstring(),
             R(diagnostics.X + 18.f, diagnostics.Y + 320.f, diagnostics.Width - 36.f, 42.f),
             8.f, muted());

    drawText(g, L"READY means: runtime + localhost API + DiT loaded + 10-second WAV self-test.",
             R(diagnostics.X + 18.f, diagnostics.GetBottom() - 46.f, diagnostics.Width - 36.f, 34.f),
             8.f, status.ready() ? Color(255, 176, 228, 156) : muted(), FontStyleBold);

    drawButton(g, R(card.X + 24.f, card.GetBottom() - 62.f, 184.f, 40.f),
               g_working ? L"PROVISIONING..." : (status.ready() ? L"VERIFY AGAIN" : L"INSTALL / VERIFY"),
               ActStartEngine, true);
    drawButton(g, R(card.X + 222.f, card.GetBottom() - 62.f, 188.f, 40.f),
               L"OPEN RUNTIME / LOGS", ActOpenRuntime);
    ReleaseDC(hwnd, dc);
}

void drawProvisionerOverlay(HWND hwnd) {
    drawProvisionerHomeOverlay(hwnd);
    drawProvisionerEngineOverlay(hwnd);
}

LRESULT CALLBACK provisionerWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == kProvisionTimer &&
            (g_screen == Screen::Home || g_screen == Screen::Engine)) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_APP_WORK_DONE: {
        WorkKind pending = WorkKind::None;
        {
            std::scoped_lock lock(g_resultMutex);
            pending = g_pendingKind;
        }
        const LRESULT result = assembleWindowProc(hwnd, msg, wParam, lParam);
        if (pending == WorkKind::EngineCheck) {
            const auto status = etherbeat::managed_ace_step_engine_status();
            if (status.ready()) {
                setStatus(L"ENGINE // REAL MODEL READY // 10-SECOND WARMUP WAV VERIFIED");
            } else if (status.state == etherbeat::EngineProvisionState::Failed) {
                setStatus(L"ENGINE // FAILED // " + wide(status.error));
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return result;
    }

    case WM_PAINT: {
        const LRESULT result = assembleWindowProc(hwnd, msg, wParam, lParam);
        drawProvisionerOverlay(hwnd);
        return result;
    }

    case WM_DESTROY:
        KillTimer(hwnd, kProvisionTimer);
        break;

    default:
        break;
    }
    return assembleWindowProc(hwnd, msg, wParam, lParam);
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
    wc.lpfnWndProc = provisionerWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(
        0, kWindowClass, L"ETHERBEAT // Alien Workshop V0.2T // ENGINE PROVISIONER",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 1040,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;

    SetTimer(hwnd, kProvisionTimer, 250, nullptr);
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
