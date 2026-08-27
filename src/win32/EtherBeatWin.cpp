#define UNICODE
#define _UNICODE
#define NOMINMAX

#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/ModelRouter.hpp"

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;
namespace fs = std::filesystem;

namespace {

constexpr wchar_t kWindowClass[] = L"EtherBeatMainWindow";
constexpr int kMinWidth = 1160;
constexpr int kMinHeight = 720;
constexpr DWORD kImmersiveDarkModeAttribute = 20;

constexpr int ID_PROMPT = 1001;
constexpr int ID_BPM = 1002;
constexpr int ID_KEY = 1003;
constexpr int ID_DURATION = 1004;
constexpr int ID_SEED = 1005;
constexpr int ID_MODE_TEXT = 1101;
constexpr int ID_MODE_VARIATION = 1102;
constexpr int ID_MODE_EXTEND = 1103;
constexpr int ID_REFERENCE = 1104;
constexpr int ID_GENERATE = 1201;
constexpr int ID_MUTATE = 1202;
constexpr int ID_OPEN_OUTPUT = 1203;

HINSTANCE g_instance{};
HWND g_window{};
HWND g_prompt{};
HWND g_bpm{};
HWND g_key{};
HWND g_duration{};
HWND g_seed{};
HFONT g_uiFont{};
HFONT g_smallFont{};
HFONT g_promptFont{};
HBRUSH g_editBrush{};
ULONG_PTR g_gdiplusToken{};

etherbeat::GenerationMode g_mode = etherbeat::GenerationMode::TextToInstrumental;
std::wstring g_referenceAudio;
std::wstring g_status = L"MODEL BRIDGE // mock-wave-48k armed";
std::wstring g_lastArtifact;
std::uint64_t g_lastSeed = 0;
bool g_generating = false;

struct Layout {
    int width{};
    int height{};
    int margin{38};
    int top{92};
    int leftW{238};
    int gap{20};
    int rightW{300};
    int centerX{};
    int centerW{};
    int rightX{};
};

Layout currentLayout() {
    RECT r{};
    GetClientRect(g_window, &r);
    Layout l;
    l.width = r.right - r.left;
    l.height = r.bottom - r.top;
    l.rightX = l.width - l.margin - l.rightW;
    l.centerX = l.margin + l.leftW + l.gap;
    l.centerW = l.rightX - l.gap - l.centerX;
    return l;
}

std::wstring getText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1u, L'\0');
    GetWindowTextW(control, buffer.data(), static_cast<int>(buffer.size()));
    return std::wstring{buffer.data()};
}

std::string toUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("Could not encode text as UTF-8");
    std::string output(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), size, nullptr, nullptr);
    return output;
}

std::wstring fromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), size);
    return output;
}

double parseDouble(HWND control, double fallback, double low, double high) {
    const std::wstring text = getText(control);
    wchar_t* end = nullptr;
    const double value = std::wcstod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value)) return fallback;
    return std::clamp(value, low, high);
}

std::uint64_t parseSeed() {
    const std::wstring text = getText(g_seed);
    if (text.empty() || text == L"random" || text == L"RANDOM") return 0;
    wchar_t* end = nullptr;
    const unsigned long long value = std::wcstoull(text.c_str(), &end, 10);
    if (end == text.c_str()) return 0;
    return static_cast<std::uint64_t>(value);
}

void setStatus(const std::wstring& text) {
    g_status = text;
    if (g_window) InvalidateRect(g_window, nullptr, FALSE);
}

void setMode(etherbeat::GenerationMode mode) {
    g_mode = mode;
    if (g_window) InvalidateRect(g_window, nullptr, FALSE);
}

const wchar_t* modeLabel(etherbeat::GenerationMode mode) {
    switch (mode) {
    case etherbeat::GenerationMode::TextToInstrumental: return L"CREATE";
    case etherbeat::GenerationMode::Variation: return L"VARIATION";
    case etherbeat::GenerationMode::Extend: return L"EXTEND";
    case etherbeat::GenerationMode::AudioToAudio: return L"REFERENCE";
    }
    return L"CREATE";
}

void applyControlFont(HWND control, HFONT font) {
    if (control && font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void layoutControls() {
    if (!g_window) return;
    const Layout l = currentLayout();

    const int leftX = l.margin + 18;
    const int fieldW = l.leftW - 36;
    MoveWindow(g_bpm, leftX, 188, fieldW, 34, TRUE);
    MoveWindow(g_key, leftX, 270, fieldW, 34, TRUE);
    MoveWindow(g_duration, leftX, 352, fieldW, 34, TRUE);
    MoveWindow(g_seed, leftX, 434, fieldW, 34, TRUE);

    const int promptX = l.centerX + 22;
    const int promptW = std::max(240, l.centerW - 44);
    MoveWindow(g_prompt, promptX, 242, promptW, 205, TRUE);
}

void roundedRect(Graphics& g, const RectF& r, REAL radius, const Color& fill, const Color& stroke, REAL strokeWidth = 1.f) {
    GraphicsPath path;
    const REAL d = radius * 2.f;
    path.AddArc(r.X, r.Y, d, d, 180.f, 90.f);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270.f, 90.f);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.f, 90.f);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90.f, 90.f);
    path.CloseFigure();
    SolidBrush brush(fill);
    g.FillPath(&brush, &path);
    if (stroke.GetA() > 0 && strokeWidth > 0.f) {
        Pen pen(stroke, strokeWidth);
        g.DrawPath(&pen, &path);
    }
}

void drawText(Graphics& g, const std::wstring& value, const RectF& rect, REAL size, const Color& color,
              INT style = FontStyleRegular, StringAlignment align = StringAlignmentNear,
              StringAlignment valign = StringAlignmentNear) {
    FontFamily family(L"Segoe UI");
    Font font(&family, size, style, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(valign);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(value.c_str(), static_cast<INT>(value.size()), &font, rect, &format, &brush);
}

void drawLabel(Graphics& g, const std::wstring& label, float x, float y, float w) {
    drawText(g, label, RectF(x, y, w, 22.f), 11.f, Color(255, 116, 110, 127), FontStyleBold);
}

void drawFieldShell(Graphics& g, float x, float y, float w, float h) {
    roundedRect(g, RectF(x, y, w, h), 13.f, Color(225, 14, 12, 20), Color(105, 69, 61, 81), 1.f);
}

Color accentPink(BYTE alpha = 255) { return Color(alpha, 229, 45, 158); }
Color accentViolet(BYTE alpha = 255) { return Color(alpha, 155, 87, 245); }
Color accentBlue(BYTE alpha = 255) { return Color(alpha, 85, 188, 245); }

void drawSignalCore(Graphics& g, const RectF& area) {
    const float cx = area.X + area.Width * .5f;
    const float cy = area.Y + 146.f;

    for (int i = 5; i >= 0; --i) {
        const float d = 62.f + static_cast<float>(i) * 24.f;
        Pen ring(i % 2 ? accentViolet(static_cast<BYTE>(38 + i * 7)) : accentPink(static_cast<BYTE>(34 + i * 7)), 1.2f);
        g.DrawEllipse(&ring, cx - d * .5f, cy - d * .37f, d, d * .74f);
    }

    GraphicsPath glowPath;
    glowPath.AddEllipse(cx - 72.f, cy - 72.f, 144.f, 144.f);
    PathGradientBrush glow(&glowPath);
    glow.SetCenterColor(g_lastArtifact.empty() ? Color(80, 108, 61, 178) : Color(118, 229, 45, 158));
    Color edge(0, 30, 8, 47);
    INT count = 1;
    glow.SetSurroundColors(&edge, &count);
    g.FillEllipse(&glow, cx - 72.f, cy - 72.f, 144.f, 144.f);

    SolidBrush core(g_lastArtifact.empty() ? Color(255, 18, 11, 29) : Color(255, 33, 9, 29));
    g.FillEllipse(&core, cx - 35.f, cy - 35.f, 70.f, 70.f);

    Pen coreLine(g_lastArtifact.empty() ? accentViolet(150) : accentPink(220), 1.4f);
    g.DrawEllipse(&coreLine, cx - 35.f, cy - 35.f, 70.f, 70.f);

    drawText(g, g_lastArtifact.empty() ? L"ARMED" : L"CAPTURED",
             RectF(cx - 70.f, cy - 12.f, 140.f, 24.f), 11.f,
             Color(255, 235, 229, 240), FontStyleBold, StringAlignmentCenter, StringAlignmentCenter);
}

void drawAnalyzer(Graphics& g, const RectF& area) {
    drawText(g, L"FREQUENCY ANALYZER", RectF(area.X + 20.f, area.Y + 20.f, area.Width - 40.f, 22.f),
             11.f, Color(255, 116, 110, 127), FontStyleBold);

    const float graphX = area.X + 20.f;
    const float graphY = area.GetBottom() - 148.f;
    const float graphW = area.Width - 40.f;
    const float graphH = 70.f;

    for (int line = 0; line <= 4; ++line) {
        const float y = graphY + graphH * static_cast<float>(line) / 4.f;
        Pen p(Color(34, 132, 122, 148), 1.f);
        g.DrawLine(&p, graphX, y, graphX + graphW, y);
    }

    constexpr int bars = 32;
    const float gap = 3.f;
    const float barW = (graphW - gap * static_cast<float>(bars - 1)) / static_cast<float>(bars);
    for (int i = 0; i < bars; ++i) {
        const float normalized = g_lastArtifact.empty() ? 0.08f : (0.08f + 0.05f * static_cast<float>((i * 7 + 3) % 5));
        const float h = graphH * normalized;
        const float x = graphX + static_cast<float>(i) * (barW + gap);
        LinearGradientBrush fill(PointF(x, graphY + graphH), PointF(x, graphY + graphH - h),
                                 accentViolet(150), accentPink(190));
        g.FillRectangle(&fill, x, graphY + graphH - h, std::max(1.f, barW), h);
    }

    drawText(g, L"40", RectF(graphX, graphY + graphH + 7.f, 40.f, 18.f), 9.f, Color(255, 93, 87, 105));
    drawText(g, L"2.2K", RectF(graphX + graphW * .43f, graphY + graphH + 7.f, 50.f, 18.f), 9.f, Color(255, 93, 87, 105));
    drawText(g, L"12K", RectF(graphX + graphW - 35.f, graphY + graphH + 7.f, 35.f, 18.f), 9.f, Color(255, 93, 87, 105), FontStyleRegular, StringAlignmentFar);

    drawText(g,
             g_lastArtifact.empty() ? L"EtherPlay FFT bridge ready for wiring" : L"mock output // silence // no spectral energy",
             RectF(area.X + 20.f, area.GetBottom() - 47.f, area.Width - 40.f, 24.f), 10.f,
             Color(255, 126, 119, 137));
}

void drawModeButton(Graphics& g, DRAWITEMSTRUCT* item) {
    const bool selected =
        (item->CtlID == ID_MODE_TEXT && g_mode == etherbeat::GenerationMode::TextToInstrumental) ||
        (item->CtlID == ID_MODE_VARIATION && g_mode == etherbeat::GenerationMode::Variation) ||
        (item->CtlID == ID_MODE_EXTEND && g_mode == etherbeat::GenerationMode::Extend) ||
        (item->CtlID == ID_REFERENCE && g_mode == etherbeat::GenerationMode::AudioToAudio);
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;

    RECT wr{};
    GetWindowRect(item->hwndItem, &wr);
    const float w = static_cast<float>(wr.right - wr.left);
    const float h = static_cast<float>(wr.bottom - wr.top);
    const RectF r(0.f, 0.f, w, h);

    Color fill = selected ? Color(230, 36, 14, 40) : Color(220, 12, 10, 17);
    Color border = selected ? accentViolet(175) : Color(90, 67, 61, 78);
    if (pressed) fill = Color(245, 46, 17, 49);
    roundedRect(g, r, 16.f, fill, border, 1.f);

    std::wstring label;
    switch (item->CtlID) {
    case ID_MODE_TEXT: label = L"create"; break;
    case ID_MODE_VARIATION: label = L"variation"; break;
    case ID_MODE_EXTEND: label = L"extend"; break;
    case ID_REFERENCE: label = L"reference"; break;
    default: break;
    }
    drawText(g, label, r, 12.f, selected ? Color(255, 246, 241, 248) : Color(255, 145, 138, 153),
             FontStyleBold, StringAlignmentCenter, StringAlignmentCenter);
}

void drawActionButton(Graphics& g, DRAWITEMSTRUCT* item) {
    RECT wr{};
    GetWindowRect(item->hwndItem, &wr);
    const float w = static_cast<float>(wr.right - wr.left);
    const float h = static_cast<float>(wr.bottom - wr.top);
    const RectF r(0.f, 0.f, w, h);
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;

    if (item->CtlID == ID_GENERATE) {
        LinearGradientBrush gradient(PointF(0.f, 0.f), PointF(w, h),
                                     pressed ? Color(255, 123, 61, 214) : Color(255, 151, 74, 238),
                                     pressed ? Color(255, 202, 37, 132) : Color(255, 229, 45, 158));
        GraphicsPath path;
        const REAL radius = 19.f;
        const REAL d = radius * 2.f;
        path.AddArc(r.X, r.Y, d, d, 180.f, 90.f);
        path.AddArc(r.GetRight() - d, r.Y, d, d, 270.f, 90.f);
        path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.f, 90.f);
        path.AddArc(r.X, r.GetBottom() - d, d, d, 90.f, 90.f);
        path.CloseFigure();
        g.FillPath(&gradient, &path);
        drawText(g, g_generating ? L"GENERATING..." : L"GENERATE", r, 14.f, Color(255, 255, 250, 253), FontStyleBold,
                 StringAlignmentCenter, StringAlignmentCenter);
    } else {
        roundedRect(g, r, 19.f, pressed ? Color(240, 31, 21, 40) : Color(225, 17, 14, 24),
                    item->CtlID == ID_MUTATE ? accentPink(120) : Color(105, 70, 64, 82), 1.f);
        const std::wstring label = item->CtlID == ID_MUTATE ? L"MUTATE" : L"OPEN OUTPUT";
        drawText(g, label, r, 12.f, Color(255, 222, 216, 228), FontStyleBold, StringAlignmentCenter, StringAlignmentCenter);
    }
}

void paintWindow(HWND window) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(window, &ps);
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    RECT client{};
    GetClientRect(window, &client);
    const float W = static_cast<float>(client.right - client.left);
    const float H = static_cast<float>(client.bottom - client.top);
    const Layout l = currentLayout();

    LinearGradientBrush background(PointF(0.f, 0.f), PointF(W, H), Color(255, 3, 2, 9), Color(255, 18, 3, 29));
    g.FillRectangle(&background, 0.f, 0.f, W, H);

    GraphicsPath hazePath;
    hazePath.AddEllipse(W * .38f, -150.f, W * .62f, 480.f);
    PathGradientBrush haze(&hazePath);
    haze.SetCenterColor(Color(36, 167, 70, 231));
    Color hazeEdge(0, 20, 4, 32);
    INT surroundCount = 1;
    haze.SetSurroundColors(&hazeEdge, &surroundCount);
    g.FillPath(&haze, &hazePath);

    drawText(g, L"ETHERBEAT", RectF(38.f, 24.f, 260.f, 34.f), 25.f, Color(255, 246, 243, 248), FontStyleBold);
    drawText(g, L"ALIEN WORKSHOP // V0.1", RectF(39.f, 56.f, 260.f, 20.f), 10.f, Color(255, 111, 104, 121), FontStyleBold);
    drawText(g, L"PRIVATE GENERATIVE MUSIC SYSTEM", RectF(W - 390.f, 31.f, 350.f, 24.f), 11.f,
             Color(255, 122, 115, 131), FontStyleBold, StringAlignmentFar);

    const RectF leftCard(static_cast<float>(l.margin), static_cast<float>(l.top), static_cast<float>(l.leftW), H - 128.f);
    const RectF centerCard(static_cast<float>(l.centerX), static_cast<float>(l.top), static_cast<float>(l.centerW), H - 128.f);
    const RectF rightCard(static_cast<float>(l.rightX), static_cast<float>(l.top), static_cast<float>(l.rightW), H - 128.f);

    roundedRect(g, leftCard, 28.f, Color(220, 7, 6, 12), Color(75, 77, 68, 91));
    roundedRect(g, centerCard, 28.f, Color(225, 8, 6, 14), Color(82, 87, 76, 101));
    roundedRect(g, rightCard, 28.f, Color(218, 8, 6, 14), Color(78, 76, 67, 91));

    drawText(g, L"SOUND DNA", RectF(leftCard.X + 18.f, 114.f, leftCard.Width - 36.f, 25.f), 12.f,
             accentPink(235), FontStyleBold);
    drawText(g, L"technical anchors", RectF(leftCard.X + 18.f, 138.f, leftCard.Width - 36.f, 20.f), 10.f,
             Color(255, 105, 98, 114));

    const float leftX = leftCard.X + 18.f;
    const float fieldW = leftCard.Width - 36.f;
    drawLabel(g, L"BPM", leftX, 165.f, fieldW); drawFieldShell(g, leftX, 184.f, fieldW, 42.f);
    drawLabel(g, L"KEY / TONAL CENTER", leftX, 247.f, fieldW); drawFieldShell(g, leftX, 266.f, fieldW, 42.f);
    drawLabel(g, L"DURATION (SECONDS)", leftX, 329.f, fieldW); drawFieldShell(g, leftX, 348.f, fieldW, 42.f);
    drawLabel(g, L"SEED", leftX, 411.f, fieldW); drawFieldShell(g, leftX, 430.f, fieldW, 42.f);

    roundedRect(g, RectF(leftX, 500.f, fieldW, 112.f), 18.f, Color(170, 23, 9, 29), Color(70, 160, 72, 205));
    drawText(g, L"PLEIADIAN LEXICON", RectF(leftX + 14.f, 514.f, fieldW - 28.f, 20.f), 10.f, accentViolet(220), FontStyleBold);
    drawText(g, L"emotion → texture → space → rhythm\nprivate labels become generation controls",
             RectF(leftX + 14.f, 540.f, fieldW - 28.f, 52.f), 11.f, Color(255, 167, 158, 176));

    drawText(g, L"GENERATION CHAMBER", RectF(centerCard.X + 22.f, 114.f, centerCard.Width - 44.f, 26.f), 13.f,
             Color(255, 237, 232, 241), FontStyleBold);
    drawText(g, L"translate feeling into sound without leaving the machine", RectF(centerCard.X + 22.f, 141.f, centerCard.Width - 44.f, 20.f), 10.f,
             Color(255, 104, 98, 114));

    drawLabel(g, L"SYNESTHESIA / PRODUCTION LANGUAGE", centerCard.X + 22.f, 213.f, centerCard.Width - 44.f);
    drawFieldShell(g, centerCard.X + 18.f, 234.f, centerCard.Width - 36.f, 221.f);

    const float infoY = H - 221.f;
    roundedRect(g, RectF(centerCard.X + 18.f, infoY, centerCard.Width - 36.f, 68.f), 18.f,
                Color(170, 11, 9, 17), Color(65, 78, 69, 89));
    drawText(g, std::wstring(L"MODE // ") + modeLabel(g_mode), RectF(centerCard.X + 34.f, infoY + 13.f, 180.f, 20.f), 10.f,
             accentViolet(220), FontStyleBold);
    drawText(g, g_referenceAudio.empty() ? L"reference audio: none" : L"reference audio attached",
             RectF(centerCard.X + 34.f, infoY + 36.f, centerCard.Width - 68.f, 18.f), 10.f, Color(255, 126, 119, 137));

    drawText(g, L"OUTPUT SIGNAL", RectF(rightCard.X + 20.f, 114.f, rightCard.Width - 40.f, 24.f), 12.f,
             accentBlue(235), FontStyleBold);
    drawSignalCore(g, rightCard);
    drawAnalyzer(g, rightCard);

    drawText(g, g_status, RectF(40.f, H - 33.f, W - 80.f, 20.f), 10.f, Color(255, 108, 101, 118));

    EndPaint(window, &ps);
}

std::wstring browseReference(HWND owner) {
    wchar_t buffer[32768]{};
    const wchar_t filter[] = L"Audio Files (*.wav;*.mp3)\0*.wav;*.mp3\0WAV Files (*.wav)\0*.wav\0MP3 Files (*.mp3)\0*.mp3\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = 32768;
    dialog.lpstrTitle = L"Choose reference audio for EtherBeat";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&dialog) ? std::wstring(buffer) : std::wstring{};
}

void openLastOutput() {
    if (g_lastArtifact.empty()) return;
    const fs::path p(g_lastArtifact);
    const fs::path dir = p.parent_path();
    ShellExecuteW(g_window, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void generate(HWND owner, bool mutationPass) {
    if (g_generating) return;
    try {
        const std::wstring promptWide = getText(g_prompt);
        const std::string prompt = toUtf8(promptWide);
        if (prompt.empty()) {
            MessageBoxW(owner, L"Give the workshop a sound description first.", L"ETHERBEAT", MB_OK | MB_ICONINFORMATION);
            return;
        }

        g_generating = true;
        setStatus(L"GENERATING // assembling private artifact...");

        etherbeat::GenerationRequest request;
        request.prompt = prompt;
        request.mode = mutationPass ? etherbeat::GenerationMode::Variation : g_mode;
        request.duration_seconds = parseDouble(g_duration, 20.0, 1.0, 600.0);
        request.bpm = parseDouble(g_bpm, 68.0, 0.0, 300.0);
        request.key = toUtf8(getText(g_key));
        request.seed = parseSeed();
        request.mutation_amount = mutationPass ? 0.72 : 0.35;
        if (!g_referenceAudio.empty()) request.reference_audio = fs::path(g_referenceAudio);

        etherbeat::ModelRouter router{etherbeat::make_default_backend()};
        const auto artifact = router.generate(request, L"generated");

        g_lastArtifact = fs::absolute(artifact.audio_path).wstring();
        g_lastSeed = artifact.resolved_seed;
        SetWindowTextW(g_seed, std::to_wstring(g_lastSeed).c_str());
        setStatus(L"CAPTURED // " + g_lastArtifact);
        g_generating = false;
        InvalidateRect(g_window, nullptr, FALSE);
    } catch (const std::exception& error) {
        g_generating = false;
        setStatus(L"GENERATION ERROR // backend returned failure");
        const std::wstring detail = fromUtf8(error.what());
        MessageBoxW(owner, (L"Generation failed.\n\n" + detail).c_str(), L"ETHERBEAT ERROR", MB_OK | MB_ICONERROR);
    }
}

HWND makeEdit(HWND parent, int id, const wchar_t* text, DWORD extraStyle = 0) {
    HWND control = CreateWindowExW(
        0, L"EDIT", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | extraStyle,
        0, 0, 10, 10, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
    applyControlFont(control, g_uiFont);
    return control;
}

HWND makeOwnerButton(HWND parent, int id, const wchar_t* label) {
    HWND button = CreateWindowW(
        L"BUTTON", label,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 10, 10, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
    applyControlFont(button, g_smallFont);
    return button;
}

void createControls(HWND window) {
    g_bpm = makeEdit(window, ID_BPM, L"68");
    g_key = makeEdit(window, ID_KEY, L"F# minor");
    g_duration = makeEdit(window, ID_DURATION, L"20");
    g_seed = makeEdit(window, ID_SEED, L"random");
    g_prompt = makeEdit(
        window, ID_PROMPT,
        L"haunted 2016 cloud-rap instrumental, enormous negative space, cold purple chrome, cheap headphones, lonely but arrogant, submerged bass, beautifully degraded",
        ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN);
    applyControlFont(g_prompt, g_promptFont);

    HWND modeText = makeOwnerButton(window, ID_MODE_TEXT, L"create");
    HWND modeVariation = makeOwnerButton(window, ID_MODE_VARIATION, L"variation");
    HWND modeExtend = makeOwnerButton(window, ID_MODE_EXTEND, L"extend");
    HWND reference = makeOwnerButton(window, ID_REFERENCE, L"reference");
    HWND generateButton = makeOwnerButton(window, ID_GENERATE, L"generate");
    HWND mutateButton = makeOwnerButton(window, ID_MUTATE, L"mutate");
    HWND openButton = makeOwnerButton(window, ID_OPEN_OUTPUT, L"open output");

    const Layout l = currentLayout();
    const int modeY = 174;
    const int modeGap = 8;
    const int modeW = std::max(70, (l.centerW - 44 - modeGap * 3) / 4);
    int x = l.centerX + 22;
    for (HWND b : {modeText, modeVariation, modeExtend, reference}) {
        MoveWindow(b, x, modeY, modeW, 32, TRUE);
        x += modeW + modeGap;
    }

    const int actionY = l.height - 137;
    const int actionX = l.centerX + 18;
    const int actionW = l.centerW - 36;
    MoveWindow(generateButton, actionX, actionY, static_cast<int>(actionW * .52), 48, TRUE);
    MoveWindow(mutateButton, actionX + static_cast<int>(actionW * .52) + 10, actionY, static_cast<int>(actionW * .23) - 5, 48, TRUE);
    MoveWindow(openButton, actionX + static_cast<int>(actionW * .75) + 10, actionY, static_cast<int>(actionW * .25) - 10, 48, TRUE);

    layoutControls();
}

void repositionButtons(HWND window) {
    const Layout l = currentLayout();
    const int modeY = 174;
    const int modeGap = 8;
    const int modeW = std::max(70, (l.centerW - 44 - modeGap * 3) / 4);
    int x = l.centerX + 22;
    for (int id : {ID_MODE_TEXT, ID_MODE_VARIATION, ID_MODE_EXTEND, ID_REFERENCE}) {
        HWND b = GetDlgItem(window, id);
        MoveWindow(b, x, modeY, modeW, 32, TRUE);
        x += modeW + modeGap;
    }

    const int actionY = l.height - 137;
    const int actionX = l.centerX + 18;
    const int actionW = l.centerW - 36;
    MoveWindow(GetDlgItem(window, ID_GENERATE), actionX, actionY, static_cast<int>(actionW * .52), 48, TRUE);
    MoveWindow(GetDlgItem(window, ID_MUTATE), actionX + static_cast<int>(actionW * .52) + 10, actionY, static_cast<int>(actionW * .23) - 5, 48, TRUE);
    MoveWindow(GetDlgItem(window, ID_OPEN_OUTPUT), actionX + static_cast<int>(actionW * .75) + 10, actionY, static_cast<int>(actionW * .25) - 10, 48, TRUE);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g_window = window;
        createControls(window);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = kMinWidth;
        info->ptMinTrackSize.y = kMinHeight;
        return 0;
    }

    case WM_SIZE:
        layoutControls();
        repositionButtons(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_MODE_TEXT:
            if (HIWORD(wParam) == BN_CLICKED) setMode(etherbeat::GenerationMode::TextToInstrumental);
            return 0;
        case ID_MODE_VARIATION:
            if (HIWORD(wParam) == BN_CLICKED) setMode(etherbeat::GenerationMode::Variation);
            return 0;
        case ID_MODE_EXTEND:
            if (HIWORD(wParam) == BN_CLICKED) setMode(etherbeat::GenerationMode::Extend);
            return 0;
        case ID_REFERENCE:
            if (HIWORD(wParam) == BN_CLICKED) {
                const auto chosen = browseReference(window);
                if (!chosen.empty()) {
                    g_referenceAudio = chosen;
                    setMode(etherbeat::GenerationMode::AudioToAudio);
                    setStatus(L"REFERENCE ATTACHED // " + fs::path(chosen).filename().wstring());
                }
            }
            return 0;
        case ID_GENERATE:
            if (HIWORD(wParam) == BN_CLICKED) generate(window, false);
            return 0;
        case ID_MUTATE:
            if (HIWORD(wParam) == BN_CLICKED) generate(window, true);
            return 0;
        case ID_OPEN_OUTPUT:
            if (HIWORD(wParam) == BN_CLICKED) openLastOutput();
            return 0;
        default:
            break;
        }
        break;

    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!item) break;
        Graphics g(item->hDC);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        if (item->CtlID >= ID_MODE_TEXT && item->CtlID <= ID_REFERENCE) drawModeButton(g, item);
        else if (item->CtlID == ID_GENERATE || item->CtlID == ID_MUTATE || item->CtlID == ID_OPEN_OUTPUT) drawActionButton(g, item);
        return TRUE;
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(238, 233, 241));
        SetBkColor(dc, RGB(14, 12, 20));
        return reinterpret_cast<LRESULT>(g_editBrush);
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paintWindow(window);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_instance = instance;

    GdiplusStartupInput gdiplusInput;
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr) != Ok) {
        MessageBoxW(nullptr, L"Could not initialize the EtherBeat renderer.", L"ETHERBEAT", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_uiFont = CreateFontW(-16, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_smallFont = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_promptFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_editBrush = CreateSolidBrush(RGB(14, 12, 20));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"Could not register ETHERBEAT window class.", L"ETHERBEAT", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND window = CreateWindowExW(
        0, kWindowClass, L"ETHERBEAT // Alien Workshop",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
        nullptr, nullptr, instance, nullptr);

    if (!window) {
        MessageBoxW(nullptr, L"Could not create ETHERBEAT.", L"ETHERBEAT", MB_OK | MB_ICONERROR);
        return 1;
    }

    BOOL dark = TRUE;
    DwmSetWindowAttribute(window, kImmersiveDarkModeAttribute, &dark, sizeof(dark));

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_editBrush) DeleteObject(g_editBrush);
    if (g_uiFont) DeleteObject(g_uiFont);
    if (g_smallFont) DeleteObject(g_smallFont);
    if (g_promptFont) DeleteObject(g_promptFont);
    GdiplusShutdown(g_gdiplusToken);

    return static_cast<int>(message.wParam);
}
