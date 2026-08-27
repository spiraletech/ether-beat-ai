#include "etherbeat/AudioAnalysis.hpp"
#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/ModelRouter.hpp"

#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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
etherbeat::AudioAnalysis g_analysis{};
std::wstring g_referenceAudio;
std::wstring g_status = L"ETHER AUDIO // choose REFERENCE to analyze a real WAV or MP3";
std::wstring g_lastArtifact;
std::uint64_t g_lastSeed = 0;
bool g_generating = false;
bool g_analyzing = false;

struct Layout {
    int width{};
    int height{};
    int margin{38};
    int top{92};
    int leftW{238};
    int gap{20};
    int rightW{316};
    int centerX{};
    int centerW{};
    int rightX{};
};

Layout layout() {
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
    return std::wstring(buffer.data());
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
    const std::wstring textValue = getText(control);
    wchar_t* end = nullptr;
    const double value = std::wcstod(textValue.c_str(), &end);
    if (end == textValue.c_str() || !std::isfinite(value)) return fallback;
    return std::clamp(value, low, high);
}

std::uint64_t parseSeed() {
    const std::wstring textValue = getText(g_seed);
    if (textValue.empty() || textValue == L"random" || textValue == L"RANDOM") return 0;
    wchar_t* end = nullptr;
    const unsigned long long value = std::wcstoull(textValue.c_str(), &end, 10);
    return end == textValue.c_str() ? 0 : static_cast<std::uint64_t>(value);
}

void setStatus(const std::wstring& value) {
    g_status = value;
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

Color pink(BYTE alpha = 255) { return Color(alpha, 229, 45, 158); }
Color violet(BYTE alpha = 255) { return Color(alpha, 155, 87, 245); }
Color blue(BYTE alpha = 255) { return Color(alpha, 85, 188, 245); }

void roundedRect(Graphics& g, const RectF& r, REAL radius, const Color& fill, const Color& stroke, REAL sw = 1.f) {
    GraphicsPath path;
    const REAL d = radius * 2.f;
    path.AddArc(r.X, r.Y, d, d, 180.f, 90.f);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270.f, 90.f);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.f, 90.f);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90.f, 90.f);
    path.CloseFigure();
    SolidBrush brush(fill);
    g.FillPath(&brush, &path);
    if (stroke.GetA() && sw > 0.f) {
        Pen pen(stroke, sw);
        g.DrawPath(&pen, &path);
    }
}

void text(Graphics& g, const std::wstring& value, const RectF& r, REAL size, const Color& color,
          INT style = FontStyleRegular, StringAlignment align = StringAlignmentNear,
          StringAlignment valign = StringAlignmentNear) {
    FontFamily family(L"Segoe UI");
    Font font(&family, size, style, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(valign);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(value.c_str(), static_cast<INT>(value.size()), &font, r, &format, &brush);
}

void label(Graphics& g, const std::wstring& value, float x, float y, float w) {
    text(g, value, RectF(x, y, w, 20.f), 10.f, Color(255, 116, 110, 127), FontStyleBold);
}

void fieldShell(Graphics& g, float x, float y, float w, float h) {
    roundedRect(g, RectF(x, y, w, h), 13.f, Color(230, 14, 12, 20), Color(105, 69, 61, 81));
}

std::wstring percent(float value) {
    return std::to_wstring(static_cast<int>(std::round(std::clamp(value, 0.f, 1.f) * 100.f))) + L"%";
}

std::wstring analysisSummary() {
    if (!g_analysis.ready) return L"NO REFERENCE CAPTURED";
    std::wostringstream out;
    out << g_analysis.sample_rate << L" Hz  //  "
        << std::fixed << std::setprecision(1) << g_analysis.duration_seconds << L"s analyzed";
    return out.str();
}

void layoutControls() {
    if (!g_window) return;
    const Layout l = layout();
    const int leftX = l.margin + 18;
    const int fieldW = l.leftW - 36;
    MoveWindow(g_bpm, leftX, 188, fieldW, 34, TRUE);
    MoveWindow(g_key, leftX, 270, fieldW, 34, TRUE);
    MoveWindow(g_duration, leftX, 352, fieldW, 34, TRUE);
    MoveWindow(g_seed, leftX, 434, fieldW, 34, TRUE);
    MoveWindow(g_prompt, l.centerX + 22, 242, std::max(240, l.centerW - 44), 205, TRUE);
}

void positionButtons(HWND window) {
    const Layout l = layout();
    const int modeGap = 8;
    const int modeW = std::max(70, (l.centerW - 44 - modeGap * 3) / 4);
    int x = l.centerX + 22;
    for (int id : {ID_MODE_TEXT, ID_MODE_VARIATION, ID_MODE_EXTEND, ID_REFERENCE}) {
        MoveWindow(GetDlgItem(window, id), x, 174, modeW, 32, TRUE);
        x += modeW + modeGap;
    }

    const int actionY = l.height - 137;
    const int actionX = l.centerX + 18;
    const int actionW = l.centerW - 36;
    MoveWindow(GetDlgItem(window, ID_GENERATE), actionX, actionY, static_cast<int>(actionW * .52), 48, TRUE);
    MoveWindow(GetDlgItem(window, ID_MUTATE), actionX + static_cast<int>(actionW * .52) + 10, actionY, static_cast<int>(actionW * .23) - 5, 48, TRUE);
    MoveWindow(GetDlgItem(window, ID_OPEN_OUTPUT), actionX + static_cast<int>(actionW * .75) + 10, actionY, static_cast<int>(actionW * .25) - 10, 48, TRUE);
}

void drawSignalCore(Graphics& g, const RectF& area) {
    const float cx = area.X + area.Width * .5f;
    const float cy = area.Y + 145.f;
    const float pulse = g_analysis.ready ? (0.75f + g_analysis.energy * 0.6f) : 0.75f;

    for (int i = 5; i >= 0; --i) {
        const float d = (62.f + static_cast<float>(i) * 24.f) * pulse;
        Pen ring(i % 2 ? violet(static_cast<BYTE>(38 + i * 7)) : pink(static_cast<BYTE>(34 + i * 7)), 1.2f);
        g.DrawEllipse(&ring, cx - d * .5f, cy - d * .37f, d, d * .74f);
    }

    GraphicsPath glowPath;
    glowPath.AddEllipse(cx - 75.f, cy - 75.f, 150.f, 150.f);
    PathGradientBrush glow(&glowPath);
    glow.SetCenterColor(g_analysis.ready ? Color(125, 229, 45, 158) : Color(70, 108, 61, 178));
    Color edge(0, 30, 8, 47);
    INT count = 1;
    glow.SetSurroundColors(&edge, &count);
    g.FillEllipse(&glow, cx - 75.f, cy - 75.f, 150.f, 150.f);

    SolidBrush core(Color(255, 20, 9, 29));
    g.FillEllipse(&core, cx - 36.f, cy - 36.f, 72.f, 72.f);
    Pen coreLine(g_analysis.ready ? pink(220) : violet(150), 1.5f);
    g.DrawEllipse(&coreLine, cx - 36.f, cy - 36.f, 72.f, 72.f);
    text(g, g_analyzing ? L"READING" : (g_analysis.ready ? L"HEARD" : L"ARMED"),
         RectF(cx - 72.f, cy - 12.f, 144.f, 24.f), 11.f, Color(255, 240, 234, 243), FontStyleBold,
         StringAlignmentCenter, StringAlignmentCenter);
}

void drawAnalyzer(Graphics& g, const RectF& area) {
    text(g, L"ETHERPLAY FFT // 32 BANDS", RectF(area.X + 20.f, area.Y + 292.f, area.Width - 40.f, 22.f),
         10.f, Color(255, 116, 110, 127), FontStyleBold);

    const float graphX = area.X + 20.f;
    const float graphY = area.Y + 322.f;
    const float graphW = area.Width - 40.f;
    const float graphH = 112.f;
    for (int line = 0; line <= 4; ++line) {
        const float y = graphY + graphH * static_cast<float>(line) / 4.f;
        Pen p(Color(32, 132, 122, 148));
        g.DrawLine(&p, graphX, y, graphX + graphW, y);
    }

    constexpr int bars = 32;
    const float gap = 2.7f;
    const float barW = (graphW - gap * static_cast<float>(bars - 1)) / static_cast<float>(bars);
    for (int i = 0; i < bars; ++i) {
        const float value = g_analysis.ready ? g_analysis.spectrum[static_cast<std::size_t>(i)] : 0.035f;
        const float h = std::max(2.f, graphH * value);
        const float x = graphX + static_cast<float>(i) * (barW + gap);
        LinearGradientBrush fill(PointF(x, graphY + graphH), PointF(x, graphY + graphH - h), violet(155), pink(215));
        g.FillRectangle(&fill, x, graphY + graphH - h, std::max(1.f, barW), h);
    }

    text(g, L"40", RectF(graphX, graphY + graphH + 6.f, 36.f, 16.f), 9.f, Color(255, 93, 87, 105));
    text(g, L"2.2K", RectF(graphX + graphW * .43f, graphY + graphH + 6.f, 46.f, 16.f), 9.f, Color(255, 93, 87, 105));
    text(g, L"12K", RectF(graphX + graphW - 36.f, graphY + graphH + 6.f, 36.f, 16.f), 9.f, Color(255, 93, 87, 105),
         FontStyleRegular, StringAlignmentFar);

    const float statsY = graphY + graphH + 34.f;
    const float statW = (graphW - 18.f) / 4.f;
    struct Stat { const wchar_t* name; float value; } stats[] = {
        {L"BASS", g_analysis.bass}, {L"MID", g_analysis.mid}, {L"AIR", g_analysis.treble}, {L"BEAT", g_analysis.beat_peak}
    };
    for (int i = 0; i < 4; ++i) {
        const float x = graphX + static_cast<float>(i) * (statW + 6.f);
        roundedRect(g, RectF(x, statsY, statW, 48.f), 12.f, Color(180, 13, 10, 19), Color(55, 76, 67, 88));
        text(g, stats[i].name, RectF(x + 7.f, statsY + 7.f, statW - 14.f, 14.f), 8.f, Color(255, 107, 101, 116), FontStyleBold);
        text(g, g_analysis.ready ? percent(stats[i].value) : L"--", RectF(x + 7.f, statsY + 22.f, statW - 14.f, 18.f),
             11.f, Color(255, 226, 219, 231), FontStyleBold);
    }

    text(g, analysisSummary(), RectF(graphX, statsY + 58.f, graphW, 20.f), 9.f,
         g_analysis.ready ? Color(255, 144, 207, 245) : Color(255, 107, 101, 116));
}

void paintWindow(HWND window) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(window, &ps);
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    RECT client{};
    GetClientRect(window, &client);
    const float W = static_cast<float>(client.right);
    const float H = static_cast<float>(client.bottom);
    const Layout l = layout();

    LinearGradientBrush background(PointF(0.f, 0.f), PointF(W, H), Color(255, 3, 2, 9), Color(255, 18, 3, 29));
    g.FillRectangle(&background, 0.f, 0.f, W, H);

    GraphicsPath hazePath;
    hazePath.AddEllipse(W * .35f, -170.f, W * .70f, 500.f);
    PathGradientBrush haze(&hazePath);
    haze.SetCenterColor(Color(38, 167, 70, 231));
    Color hazeEdge(0, 20, 4, 32);
    INT count = 1;
    haze.SetSurroundColors(&hazeEdge, &count);
    g.FillPath(&haze, &hazePath);

    text(g, L"ETHERBEAT", RectF(38.f, 23.f, 260.f, 34.f), 25.f, Color(255, 246, 243, 248), FontStyleBold);
    text(g, L"ALIEN WORKSHOP // V0.1A", RectF(39.f, 56.f, 280.f, 20.f), 10.f, Color(255, 111, 104, 121), FontStyleBold);
    text(g, L"PRIVATE GENERATIVE MUSIC SYSTEM", RectF(W - 390.f, 31.f, 350.f, 24.f), 11.f,
         Color(255, 122, 115, 131), FontStyleBold, StringAlignmentFar);

    const RectF leftCard(static_cast<float>(l.margin), static_cast<float>(l.top), static_cast<float>(l.leftW), H - 128.f);
    const RectF centerCard(static_cast<float>(l.centerX), static_cast<float>(l.top), static_cast<float>(l.centerW), H - 128.f);
    const RectF rightCard(static_cast<float>(l.rightX), static_cast<float>(l.top), static_cast<float>(l.rightW), H - 128.f);
    roundedRect(g, leftCard, 28.f, Color(220, 7, 6, 12), Color(75, 77, 68, 91));
    roundedRect(g, centerCard, 28.f, Color(225, 8, 6, 14), Color(82, 87, 76, 101));
    roundedRect(g, rightCard, 28.f, Color(218, 8, 6, 14), Color(78, 76, 67, 91));

    const float leftX = leftCard.X + 18.f;
    const float fieldW = leftCard.Width - 36.f;
    text(g, L"SOUND DNA", RectF(leftX, 114.f, fieldW, 24.f), 12.f, pink(235), FontStyleBold);
    text(g, L"technical anchors", RectF(leftX, 139.f, fieldW, 18.f), 10.f, Color(255, 105, 98, 114));
    label(g, L"BPM", leftX, 165.f, fieldW); fieldShell(g, leftX, 184.f, fieldW, 42.f);
    label(g, L"KEY / TONAL CENTER", leftX, 247.f, fieldW); fieldShell(g, leftX, 266.f, fieldW, 42.f);
    label(g, L"DURATION (SECONDS)", leftX, 329.f, fieldW); fieldShell(g, leftX, 348.f, fieldW, 42.f);
    label(g, L"SEED", leftX, 411.f, fieldW); fieldShell(g, leftX, 430.f, fieldW, 42.f);

    roundedRect(g, RectF(leftX, 500.f, fieldW, 112.f), 18.f, Color(170, 23, 9, 29), Color(70, 160, 72, 205));
    text(g, L"PLEIADIAN LEXICON", RectF(leftX + 14.f, 514.f, fieldW - 28.f, 20.f), 10.f, violet(220), FontStyleBold);
    text(g, L"emotion → texture → space → rhythm\nprivate language becomes model control",
         RectF(leftX + 14.f, 540.f, fieldW - 28.f, 52.f), 11.f, Color(255, 167, 158, 176));

    text(g, L"GENERATION CHAMBER", RectF(centerCard.X + 22.f, 114.f, centerCard.Width - 44.f, 25.f), 13.f,
         Color(255, 237, 232, 241), FontStyleBold);
    text(g, L"reference mode now contains the real EtherPlay audio ear", RectF(centerCard.X + 22.f, 141.f, centerCard.Width - 44.f, 19.f),
         10.f, Color(255, 104, 98, 114));
    label(g, L"SYNESTHESIA / PRODUCTION LANGUAGE", centerCard.X + 22.f, 213.f, centerCard.Width - 44.f);
    fieldShell(g, centerCard.X + 18.f, 234.f, centerCard.Width - 36.f, 221.f);

    const float infoY = H - 221.f;
    roundedRect(g, RectF(centerCard.X + 18.f, infoY, centerCard.Width - 36.f, 68.f), 18.f,
                Color(170, 11, 9, 17), Color(65, 78, 69, 89));
    text(g, std::wstring(L"MODE // ") + modeLabel(g_mode), RectF(centerCard.X + 34.f, infoY + 13.f, 180.f, 20.f),
         10.f, violet(220), FontStyleBold);
    const std::wstring referenceLabel = g_referenceAudio.empty()
        ? L"reference audio: click REFERENCE to choose a track"
        : L"reference // " + fs::path(g_referenceAudio).filename().wstring();
    text(g, referenceLabel, RectF(centerCard.X + 34.f, infoY + 36.f, centerCard.Width - 68.f, 18.f), 10.f,
         g_analysis.ready ? Color(255, 148, 207, 245) : Color(255, 126, 119, 137));

    text(g, L"OUTPUT SIGNAL", RectF(rightCard.X + 20.f, 114.f, rightCard.Width - 40.f, 24.f), 12.f, blue(235), FontStyleBold);
    drawSignalCore(g, rightCard);
    drawAnalyzer(g, rightCard);
    text(g, g_status, RectF(40.f, H - 33.f, W - 80.f, 20.f), 10.f, Color(255, 115, 108, 126));

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
    dialog.lpstrTitle = L"Choose audio for EtherBeat frequency analysis";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&dialog) ? std::wstring(buffer) : std::wstring{};
}

void analyzeReference(HWND owner, const std::wstring& chosen) {
    g_referenceAudio = chosen;
    setMode(etherbeat::GenerationMode::AudioToAudio);
    g_analyzing = true;
    g_analysis = {};
    setStatus(L"ETHER AUDIO // decoding PCM + running 1024-point FFT...");
    UpdateWindow(owner);

    g_analysis = etherbeat::analyze_audio_file(fs::path(chosen));
    g_analyzing = false;
    if (g_analysis.ready) {
        std::wostringstream status;
        status << L"REFERENCE HEARD // " << fs::path(chosen).filename().wstring()
               << L" // " << g_analysis.analyzed_windows << L" FFT windows";
        setStatus(status.str());
    } else {
        setStatus(L"ANALYSIS ERROR // " + fromUtf8(g_analysis.error));
        MessageBoxW(owner, (L"EtherBeat could not analyze this track.\n\n" + fromUtf8(g_analysis.error)).c_str(),
                    L"ETHER AUDIO", MB_OK | MB_ICONERROR);
    }
    InvalidateRect(owner, nullptr, FALSE);
}

void openLastOutput() {
    if (g_lastArtifact.empty()) return;
    const fs::path dir = fs::path(g_lastArtifact).parent_path();
    ShellExecuteW(g_window, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void generate(HWND owner, bool mutationPass) {
    if (g_generating) return;
    try {
        const std::string promptValue = toUtf8(getText(g_prompt));
        if (promptValue.empty()) {
            MessageBoxW(owner, L"Give the workshop a sound description first.", L"ETHERBEAT", MB_OK | MB_ICONINFORMATION);
            return;
        }

        g_generating = true;
        setStatus(L"MOCK MODEL // writing a valid 48 kHz prototype artifact...");
        etherbeat::GenerationRequest request;
        request.prompt = promptValue;
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
        setStatus(L"MOCK ARTIFACT CAPTURED // real AI model bridge is the next engine layer");
        g_generating = false;
        InvalidateRect(owner, nullptr, FALSE);
    } catch (const std::exception& error) {
        g_generating = false;
        setStatus(L"GENERATION ERROR // backend returned failure");
        MessageBoxW(owner, (L"Generation failed.\n\n" + fromUtf8(error.what())).c_str(), L"ETHERBEAT ERROR", MB_OK | MB_ICONERROR);
    }
}

HWND makeEdit(HWND parent, int id, const wchar_t* initial, DWORD extraStyle = 0) {
    HWND control = CreateWindowExW(0, L"EDIT", initial,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | extraStyle,
        0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
    return control;
}

HWND makeButton(HWND parent, int id, const wchar_t* labelText) {
    HWND button = CreateWindowW(L"BUTTON", labelText,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_smallFont), TRUE);
    return button;
}

void createControls(HWND window) {
    g_bpm = makeEdit(window, ID_BPM, L"68");
    g_key = makeEdit(window, ID_KEY, L"F# minor");
    g_duration = makeEdit(window, ID_DURATION, L"20");
    g_seed = makeEdit(window, ID_SEED, L"random");
    g_prompt = makeEdit(window, ID_PROMPT,
        L"haunted 2016 cloud-rap instrumental, enormous negative space, cold purple chrome, cheap headphones, lonely but arrogant, submerged bass, beautifully degraded",
        ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN);
    SendMessageW(g_prompt, WM_SETFONT, reinterpret_cast<WPARAM>(g_promptFont), TRUE);

    makeButton(window, ID_MODE_TEXT, L"create");
    makeButton(window, ID_MODE_VARIATION, L"variation");
    makeButton(window, ID_MODE_EXTEND, L"extend");
    makeButton(window, ID_REFERENCE, L"reference");
    makeButton(window, ID_GENERATE, L"generate");
    makeButton(window, ID_MUTATE, L"mutate");
    makeButton(window, ID_OPEN_OUTPUT, L"open output");

    layoutControls();
    positionButtons(window);
}

void drawButton(Graphics& g, DRAWITEMSTRUCT* item) {
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
        const REAL d = 38.f;
        path.AddArc(0.f, 0.f, d, d, 180.f, 90.f);
        path.AddArc(w - d, 0.f, d, d, 270.f, 90.f);
        path.AddArc(w - d, h - d, d, d, 0.f, 90.f);
        path.AddArc(0.f, h - d, d, d, 90.f, 90.f);
        path.CloseFigure();
        g.FillPath(&gradient, &path);
        text(g, g_generating ? L"GENERATING..." : L"GENERATE", r, 14.f, Color(255, 255, 250, 253), FontStyleBold,
             StringAlignmentCenter, StringAlignmentCenter);
        return;
    }

    if (item->CtlID == ID_MUTATE || item->CtlID == ID_OPEN_OUTPUT) {
        roundedRect(g, r, 18.f, pressed ? Color(240, 31, 21, 40) : Color(225, 17, 14, 24),
                    item->CtlID == ID_MUTATE ? pink(120) : Color(105, 70, 64, 82));
        text(g, item->CtlID == ID_MUTATE ? L"MUTATE" : L"OPEN OUTPUT", r, 11.f, Color(255, 222, 216, 228), FontStyleBold,
             StringAlignmentCenter, StringAlignmentCenter);
        return;
    }

    const bool selected =
        (item->CtlID == ID_MODE_TEXT && g_mode == etherbeat::GenerationMode::TextToInstrumental) ||
        (item->CtlID == ID_MODE_VARIATION && g_mode == etherbeat::GenerationMode::Variation) ||
        (item->CtlID == ID_MODE_EXTEND && g_mode == etherbeat::GenerationMode::Extend) ||
        (item->CtlID == ID_REFERENCE && g_mode == etherbeat::GenerationMode::AudioToAudio);
    roundedRect(g, r, 15.f,
        pressed ? Color(245, 46, 17, 49) : (selected ? Color(230, 36, 14, 40) : Color(220, 12, 10, 17)),
        selected ? violet(175) : Color(90, 67, 61, 78));
    std::wstring title;
    if (item->CtlID == ID_MODE_TEXT) title = L"create";
    else if (item->CtlID == ID_MODE_VARIATION) title = L"variation";
    else if (item->CtlID == ID_MODE_EXTEND) title = L"extend";
    else title = L"reference";
    text(g, title, r, 11.f, selected ? Color(255, 246, 241, 248) : Color(255, 145, 138, 153), FontStyleBold,
         StringAlignmentCenter, StringAlignmentCenter);
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
        positionButtons(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_MODE_TEXT: if (HIWORD(wParam) == BN_CLICKED) setMode(etherbeat::GenerationMode::TextToInstrumental); return 0;
        case ID_MODE_VARIATION: if (HIWORD(wParam) == BN_CLICKED) setMode(etherbeat::GenerationMode::Variation); return 0;
        case ID_MODE_EXTEND: if (HIWORD(wParam) == BN_CLICKED) setMode(etherbeat::GenerationMode::Extend); return 0;
        case ID_REFERENCE:
            if (HIWORD(wParam) == BN_CLICKED) {
                const std::wstring chosen = browseReference(window);
                if (!chosen.empty()) analyzeReference(window, chosen);
            }
            return 0;
        case ID_GENERATE: if (HIWORD(wParam) == BN_CLICKED) generate(window, false); return 0;
        case ID_MUTATE: if (HIWORD(wParam) == BN_CLICKED) generate(window, true); return 0;
        case ID_OPEN_OUTPUT: if (HIWORD(wParam) == BN_CLICKED) openLastOutput(); return 0;
        default: break;
        }
        break;
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!item) break;
        Graphics g(item->hDC);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        drawButton(g, item);
        return TRUE;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(238, 233, 241));
        SetBkColor(dc, RGB(14, 12, 20));
        return reinterpret_cast<LRESULT>(g_editBrush);
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paintWindow(window); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_instance = instance;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    GdiplusStartupInput gdiplusInput;
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr) != Ok) {
        MessageBoxW(nullptr, L"Could not initialize the EtherBeat renderer.", L"ETHERBEAT", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(com)) CoUninitialize();
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
        GdiplusShutdown(g_gdiplusToken);
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }

    HWND window = CreateWindowExW(0, kWindowClass, L"ETHERBEAT // Alien Workshop",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
        nullptr, nullptr, instance, nullptr);
    if (!window) {
        MessageBoxW(nullptr, L"Could not create ETHERBEAT.", L"ETHERBEAT", MB_OK | MB_ICONERROR);
        GdiplusShutdown(g_gdiplusToken);
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }

    BOOL dark = TRUE;
    DwmSetWindowAttribute(window, kImmersiveDarkModeAttribute, &dark, sizeof(dark));
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_editBrush) DeleteObject(g_editBrush);
    if (g_uiFont) DeleteObject(g_uiFont);
    if (g_smallFont) DeleteObject(g_smallFont);
    if (g_promptFont) DeleteObject(g_promptFont);
    GdiplusShutdown(g_gdiplusToken);
    if (SUCCEEDED(com)) CoUninitialize();
    return static_cast<int>(msg.wParam);
}
