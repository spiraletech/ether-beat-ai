#define UNICODE
#define _UNICODE
#define NOMINMAX

#include "etherbeat/AudioAnalysis.hpp"
#include "etherbeat/AceStepEngineManager.hpp"
#include "etherbeat/EtherCompare.hpp"
#include "etherbeat/EtherControlUi.hpp"
#include "etherbeat/EtherDNA.hpp"
#include "etherbeat/EtherSearch.hpp"
#include "etherbeat/EtherVersions.hpp"
#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/ModelRouter.hpp"

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")

using namespace Gdiplus;
namespace fs = std::filesystem;

namespace {

constexpr wchar_t kWindowClass[] = L"EtherBeatTabbedWindow";
constexpr DWORD kImmersiveDarkModeAttribute = 20;
constexpr int kMinWidth = 1120;
constexpr int kMinHeight = 840;
constexpr UINT WM_APP_WORK_DONE = WM_APP + 41;

constexpr int ID_PROMPT = 2001;
constexpr int ID_BPM = 2002;
constexpr int ID_KEY = 2003;
constexpr int ID_DURATION = 2004;
constexpr int ID_SEED = 2005;
constexpr int ID_CONTROL_PROMPT = 2010;
constexpr int ID_CONTROL_STRENGTH = 2011;
constexpr int ID_CONTROL_START = 2012;
constexpr int ID_CONTROL_END = 2013;

enum class Screen { Home, Create, Library, NowPlaying, SongLab, Engine };
enum class WorkKind { None, Generate, AnalyzeReference, EngineCheck, ControlVariation, ControlReplace };

enum Action {
    ActTabHome = 1,
    ActTabCreate,
    ActTabLibrary,
    ActTabNowPlaying,
    ActTabSongLab,
    ActTabEngine,
    ActGenerate = 100,
    ActChooseReference,
    ActOpenLibrary,
    ActPlay,
    ActStop,
    ActStartEngine,
    ActOpenRuntime,
    ActVariation,
    ActReplaceSection,
    ActVersionParent,
    ActVersionPrevChild,
    ActVersionNextChild,
    ActVersionHead,
    ActVersionPromote,
    ActCompareA,
    ActCompareB,
    ActCompareToggle,
    ActLibraryBase = 1000
};

struct HitZone { RectF rect; int action; };

HINSTANCE g_instance{};
HWND g_window{};
HWND g_prompt{};
HWND g_bpm{};
HWND g_key{};
HWND g_duration{};
HWND g_seed{};
HWND g_controlPrompt{};
HWND g_controlStrength{};
HWND g_controlStart{};
HWND g_controlEnd{};
HFONT g_uiFont{};
HFONT g_promptFont{};
HBRUSH g_editBrush{};
ULONG_PTR g_gdiplus{};

Screen g_screen = Screen::Home;
std::vector<HitZone> g_hits;
std::vector<fs::path> g_library;
fs::path g_nowPlaying;
fs::path g_reference;
etherbeat::AudioAnalysis g_nowAnalysis{};
etherbeat::AudioAnalysis g_referenceAnalysis{};
std::optional<etherbeat::VersionLineage> g_versionLineage;
std::optional<etherbeat::EtherCompareResult> g_compareResult;
fs::path g_compareB;
std::wstring g_compareRelationship;
bool g_compareAuditionB = false;
std::wstring g_status = L"ETHERBEAT // READY";
std::atomic<bool> g_working{false};
std::mutex g_resultMutex;
WorkKind g_pendingKind = WorkKind::None;
bool g_pendingSuccess = false;
std::wstring g_pendingError;
fs::path g_pendingArtifact;
etherbeat::AudioAnalysis g_pendingAnalysis{};
std::uint64_t g_pendingSeed = 0;

Color amber(BYTE a = 255) { return Color(a, 242, 195, 61); }
Color warm(BYTE a = 255) { return Color(a, 244, 242, 235); }
Color muted(BYTE a = 255) { return Color(a, 142, 134, 116); }
Color panel(BYTE a = 248) { return Color(a, 7, 7, 8); }
Color violet(BYTE a = 255) { return Color(a, 151, 88, 236); }
Color pink(BYTE a = 255) { return Color(a, 225, 55, 151); }
RectF R(float x, float y, float w, float h) { return RectF(x, y, w, h); }

void roundRect(Graphics& g, const RectF& r, float radius, Color fill,
               Color stroke = Color(0, 0, 0, 0), float strokeWidth = 1.f) {
    GraphicsPath p;
    const float d = radius * 2.f;
    p.AddArc(r.X, r.Y, d, d, 180.f, 90.f);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270.f, 90.f);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.f, 90.f);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90.f, 90.f);
    p.CloseFigure();
    SolidBrush b(fill);
    g.FillPath(&b, &p);
    if (stroke.GetA()) {
        Pen pen(stroke, strokeWidth);
        g.DrawPath(&pen, &p);
    }
}

void drawText(Graphics& g, const std::wstring& value, const RectF& r, float size, Color color,
              int style = FontStyleRegular, StringAlignment h = StringAlignmentNear,
              StringAlignment v = StringAlignmentNear) {
    FontFamily family(L"Segoe UI");
    Font font(&family, size, style, UnitPixel);
    SolidBrush brush(color);
    StringFormat fmt;
    fmt.SetAlignment(h);
    fmt.SetLineAlignment(v);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(value.c_str(), -1, &font, r, &fmt, &brush);
}

void addHit(const RectF& rect, int action) { g_hits.push_back({rect, action}); }
bool hit(const RectF& r, int x, int y) { return x >= r.X && x <= r.GetRight() && y >= r.Y && y <= r.GetBottom(); }

std::wstring getText(HWND control) {
    if (!control) return {};
    const int n = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer(static_cast<std::size_t>(n) + 1u, L'\0');
    GetWindowTextW(control, buffer.data(), static_cast<int>(buffer.size()));
    return std::wstring(buffer.data());
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(0, n)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), n);
    return out;
}

double parseDouble(HWND control, double fallback, double lo, double hi) {
    const auto text = getText(control);
    wchar_t* end = nullptr;
    const double value = std::wcstod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value)) return fallback;
    return std::clamp(value, lo, hi);
}

std::uint64_t parseSeed() {
    const auto text = getText(g_seed);
    if (text.empty() || text == L"random" || text == L"RANDOM") return 0;
    wchar_t* end = nullptr;
    const auto value = std::wcstoull(text.c_str(), &end, 10);
    return end == text.c_str() ? 0 : static_cast<std::uint64_t>(value);
}

fs::path localRoot() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw)) || !raw) {
        return fs::current_path() / L"EtherBeatData";
    }
    fs::path p(raw);
    CoTaskMemFree(raw);
    return p / L"EtherTech" / L"EtherBeat";
}

fs::path libraryRoot() {
    auto p = localRoot() / L"library";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

void setStatus(const std::wstring& value) {
    g_status = value;
    if (g_window) InvalidateRect(g_window, nullptr, FALSE);
}

void stopPlayback() {
    mciSendStringW(L"stop etherbeat_now", nullptr, 0, nullptr);
    mciSendStringW(L"close etherbeat_now", nullptr, 0, nullptr);
}

unsigned long playbackPositionMs() {
    wchar_t buffer[64]{};
    if (mciSendStringW(L"status etherbeat_now position", buffer, 64, nullptr) != 0) return 0;
    return std::wcstoul(buffer, nullptr, 10);
}

bool playbackIsPlaying() {
    wchar_t buffer[64]{};
    if (mciSendStringW(L"status etherbeat_now mode", buffer, 64, nullptr) != 0) return false;
    return _wcsicmp(buffer, L"playing") == 0;
}

void playPathAt(const fs::path& path, unsigned long positionMs, bool startPlayback = true) {
    if (path.empty() || !fs::exists(path)) return;
    stopPlayback();
    const std::wstring open = L"open \"" + path.wstring() + L"\" alias etherbeat_now";
    if (mciSendStringW(open.c_str(), nullptr, 0, nullptr) != 0) return;
    mciSendStringW(L"set etherbeat_now time format milliseconds", nullptr, 0, nullptr);
    if (positionMs > 0) {
        const std::wstring seek = L"seek etherbeat_now to " + std::to_wstring(positionMs);
        if (mciSendStringW(seek.c_str(), nullptr, 0, nullptr) != 0) {
            mciSendStringW(L"seek etherbeat_now to start", nullptr, 0, nullptr);
        }
    }
    if (startPlayback) mciSendStringW(L"play etherbeat_now", nullptr, 0, nullptr);
}

void playPath(const fs::path& path) { playPathAt(path, 0, true); }

fs::path activeAuditionPath() {
    if (g_compareAuditionB && !g_compareB.empty()) return g_compareB;
    return g_nowPlaying;
}

void refreshLibrary() {
    g_library.clear();
    std::error_code ec;
    const auto root = libraryRoot();
    for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        auto ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext == L".wav" || ext == L".mp3" || ext == L".flac") g_library.push_back(entry.path());
    }
    std::sort(g_library.begin(), g_library.end(), [](const fs::path& a, const fs::path& b) {
        std::error_code ea, eb;
        const auto ta = fs::last_write_time(a, ea);
        const auto tb = fs::last_write_time(b, eb);
        if (ea || eb) return a.filename().wstring() > b.filename().wstring();
        return ta > tb;
    });
}

void showCreateControls(bool show) {
    const int cmd = show ? SW_SHOW : SW_HIDE;
    for (HWND h : {g_prompt, g_bpm, g_key, g_duration, g_seed}) if (h) ShowWindow(h, cmd);
}

void showControlControls(bool show) {
    const int cmd = show ? SW_SHOW : SW_HIDE;
    for (HWND h : {g_controlPrompt, g_controlStrength, g_controlStart, g_controlEnd}) if (h) ShowWindow(h, cmd);
}

void setScreen(Screen screen) {
    g_screen = screen;
    showCreateControls(screen == Screen::Create);
    showControlControls(screen == Screen::NowPlaying && !g_nowPlaying.empty());
    if (screen == Screen::Library) refreshLibrary();
    InvalidateRect(g_window, nullptr, FALSE);
}

void syncControlWindowToTrack() {
    if (!g_controlStart || !g_controlEnd) return;
    SetWindowTextW(g_controlStart, L"0.00");
    const double duration = g_nowAnalysis.ready ? g_nowAnalysis.duration_seconds : 8.0;
    const double end = std::clamp(duration, 0.25, 8.0);
    std::wostringstream value;
    value << std::fixed << std::setprecision(2) << end;
    SetWindowTextW(g_controlEnd, value.str().c_str());
}

void ensureDnaFor(const fs::path& path) {
    if (path.empty() || etherbeat::load_ether_dna_for_audio(path)) return;
    const auto analysis = etherbeat::analyze_audio_file(path);
    if (!analysis.ready) return;
    const auto dna = etherbeat::make_ether_dna(path, analysis);
    static_cast<void>(etherbeat::save_ether_dna(dna, etherbeat::ether_dna_sidecar_path(path)));
}

void refreshVersionState() {
    g_versionLineage.reset();
    if (g_nowPlaying.empty()) return;
    try {
        etherbeat::EtherVersions versions(libraryRoot());
        versions.ensure_root(g_nowPlaying);
        g_versionLineage = versions.lineage_for_audio(g_nowPlaying);
    } catch (...) {
        g_versionLineage.reset();
    }
}

std::vector<etherbeat::VersionRecord> versionBranchOptions() {
    if (!g_versionLineage || g_nowPlaying.empty()) return {};
    try {
        etherbeat::EtherVersions versions(libraryRoot());
        if (g_versionLineage->parent) return versions.lineage_for_audio(g_versionLineage->parent->audio_path).children;
        return g_versionLineage->children;
    } catch (...) {
        return {};
    }
}

fs::path chooseCompareB(std::wstring& relationship) {
    relationship.clear();
    if (!g_versionLineage || g_nowPlaying.empty()) return {};
    try {
        etherbeat::EtherVersions versions(libraryRoot());
        const auto head = versions.promoted_audio_for(g_nowPlaying);
        if (!head.empty() && head != g_nowPlaying && fs::exists(head)) {
            relationship = L"HEAD";
            return head;
        }

        const auto branches = versionBranchOptions();
        if (!branches.empty()) {
            const auto it = std::find_if(branches.begin(), branches.end(), [](const etherbeat::VersionRecord& r) {
                return r.audio_path == g_nowPlaying;
            });
            if (it != branches.end() && branches.size() > 1) {
                const auto index = static_cast<std::size_t>(std::distance(branches.begin(), it));
                const auto& next = branches[(index + 1u) % branches.size()];
                relationship = L"SIBLING";
                return next.audio_path;
            }
            if (!g_versionLineage->parent && branches.front().audio_path != g_nowPlaying) {
                relationship = L"CHILD";
                return branches.front().audio_path;
            }
        }

        if (g_versionLineage->parent && fs::exists(g_versionLineage->parent->audio_path)) {
            relationship = L"PARENT";
            return g_versionLineage->parent->audio_path;
        }
    } catch (...) {}
    return {};
}

void refreshCompareState() {
    g_compareResult.reset();
    g_compareB.clear();
    g_compareRelationship.clear();
    g_compareAuditionB = false;
    if (g_nowPlaying.empty()) return;

    g_compareB = chooseCompareB(g_compareRelationship);
    if (g_compareB.empty() || g_compareB == g_nowPlaying) return;
    ensureDnaFor(g_nowPlaying);
    ensureDnaFor(g_compareB);
    g_compareResult = etherbeat::compare_audio_dna(g_nowPlaying, g_compareB);
}

bool loadNowPlaying(const fs::path& path, bool autoplay = true) {
    if (path.empty() || !fs::exists(path)) return false;
    stopPlayback();
    g_nowPlaying = path;
    g_nowAnalysis = etherbeat::analyze_audio_file(g_nowPlaying);
    if (g_nowAnalysis.ready) {
        const auto dna = etherbeat::make_ether_dna(g_nowPlaying, g_nowAnalysis);
        static_cast<void>(etherbeat::save_ether_dna(dna, etherbeat::ether_dna_sidecar_path(g_nowPlaying)));
    }
    syncControlWindowToTrack();
    refreshVersionState();
    refreshCompareState();
    setScreen(Screen::NowPlaying);
    if (autoplay) playPath(g_nowPlaying);
    return true;
}

void navigateVersionParent() {
    if (!g_versionLineage || !g_versionLineage->parent) { setStatus(L"ETHERVERSIONS // already at root"); return; }
    const auto target = g_versionLineage->parent->audio_path;
    if (loadNowPlaying(target)) setStatus(L"ETHERVERSIONS // PARENT // " + target.filename().wstring());
}

void navigateVersionHead() {
    if (g_nowPlaying.empty()) return;
    try {
        etherbeat::EtherVersions versions(libraryRoot());
        const auto target = versions.promoted_audio_for(g_nowPlaying);
        if (target.empty()) { setStatus(L"ETHERVERSIONS // no promoted HEAD found"); return; }
        if (target == g_nowPlaying) { setStatus(L"ETHERVERSIONS // current version is HEAD"); return; }
        if (loadNowPlaying(target)) setStatus(L"ETHERVERSIONS // HEAD // " + target.filename().wstring());
    } catch (const std::exception& e) { setStatus(L"ETHERVERSIONS // " + wide(e.what())); }
}

void promoteCurrentVersion() {
    if (g_nowPlaying.empty()) return;
    try {
        etherbeat::EtherVersions versions(libraryRoot());
        versions.ensure_root(g_nowPlaying);
        versions.promote(g_nowPlaying);
        refreshVersionState();
        refreshCompareState();
        setStatus(L"ETHERVERSIONS // PROMOTED HEAD // " + g_nowPlaying.filename().wstring());
    } catch (const std::exception& e) { setStatus(L"ETHERVERSIONS // " + wide(e.what())); }
}

void navigateVersionBranch(int direction) {
    if (!g_versionLineage) { setStatus(L"ETHERVERSIONS // no lineage available"); return; }
    const auto options = versionBranchOptions();
    if (options.empty()) {
        setStatus(g_versionLineage->parent ? L"ETHERVERSIONS // no sibling branches" : L"ETHERVERSIONS // root has no child branches yet");
        return;
    }
    std::size_t targetIndex = direction >= 0 ? 0u : options.size() - 1u;
    if (g_versionLineage->parent) {
        const auto it = std::find_if(options.begin(), options.end(), [](const etherbeat::VersionRecord& record) { return record.audio_path == g_nowPlaying; });
        if (it != options.end()) {
            const auto index = static_cast<std::size_t>(std::distance(options.begin(), it));
            targetIndex = direction >= 0 ? (index + 1u) % options.size() : (index + options.size() - 1u) % options.size();
        }
    }
    const auto target = options[targetIndex].audio_path;
    if (loadNowPlaying(target)) setStatus(L"ETHERVERSIONS // BRANCH " + std::to_wstring(targetIndex + 1u) + L"/" + std::to_wstring(options.size()));
}

void auditionCompare(bool bSide) {
    if (!g_compareResult || g_compareB.empty()) {
        setStatus(L"ETHERCOMPARE // no alternate HEAD/sibling/parent to audition");
        return;
    }
    const unsigned long position = playbackPositionMs();
    const fs::path target = bSide ? g_compareB : g_nowPlaying;
    playPathAt(target, position, true);
    g_compareAuditionB = bSide;
    setStatus(std::wstring(L"ETHERCOMPARE // ") + (bSide ? L"B " : L"A ") + target.filename().wstring() + L" // synchronized");
    InvalidateRect(g_window, nullptr, FALSE);
}

void toggleCompare() { auditionCompare(!g_compareAuditionB); }

std::wstring chooseAudio(HWND owner) {
    wchar_t buffer[32768]{};
    const wchar_t filter[] = L"Audio (*.wav;*.mp3;*.flac)\0*.wav;*.mp3;*.flac\0All files\0*.*\0\0";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = 32768;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = L"Choose audio for EtherBeat Song Lab";
    return GetOpenFileNameW(&ofn) ? std::wstring(buffer) : std::wstring{};
}

void postWork(WorkKind kind, bool success, const std::wstring& error, const fs::path& artifact,
              const etherbeat::AudioAnalysis& analysis, std::uint64_t seed) {
    {
        std::scoped_lock lock(g_resultMutex);
        g_pendingKind = kind;
        g_pendingSuccess = success;
        g_pendingError = error;
        g_pendingArtifact = artifact;
        g_pendingAnalysis = analysis;
        g_pendingSeed = seed;
    }
    if (g_window) PostMessageW(g_window, WM_APP_WORK_DONE, 0, 0);
}

void startGenerate() {
    if (g_working.exchange(true)) return;
    etherbeat::GenerationRequest request;
    request.prompt = utf8(getText(g_prompt));
    if (request.prompt.empty()) { g_working = false; MessageBoxW(g_window, L"Describe the beat first.", L"ETHERBEAT", MB_OK | MB_ICONINFORMATION); return; }
    request.duration_seconds = parseDouble(g_duration, 20.0, 10.0, 600.0);
    request.bpm = parseDouble(g_bpm, 68.0, 30.0, 300.0);
    request.key = utf8(getText(g_key));
    request.seed = parseSeed();
    request.mode = etherbeat::GenerationMode::TextToInstrumental;
    setStatus(etherbeat::managed_ace_step_runtime_installed() ? L"ETHERSEARCH x4 // generating drafts, measuring DNA, ranking..." : L"FIRST RUN // acquiring private model runtime, then EtherSearch x4...");

    std::thread([request] {
        try {
            auto router = etherbeat::make_default_router();
            etherbeat::EtherSearch search;
            const auto result = search.run(router, request, libraryRoot(),
                [](const fs::path& audio) { return etherbeat::analyze_audio_file(audio); },
                etherbeat::SearchOptions{ .draft = etherbeat::DraftOptions{.candidate_count = 4, .continue_after_failure = true}, .critic = etherbeat::CriticWeights{} });
            if (!result.has_winner() || result.winner_audio_path.empty()) throw std::runtime_error("EtherSearch produced no analyzable winner");
            const auto analysis = etherbeat::analyze_audio_file(result.winner_audio_path);
            postWork(WorkKind::Generate, true, L"", result.winner_audio_path, analysis, result.winner_seed);
        } catch (const std::exception& e) { postWork(WorkKind::Generate, false, wide(e.what()), {}, {}, 0); }
    }).detach();
}

void startControl(etherbeat::ControlUiAction action) {
    if (g_working.load()) return;
    if (g_nowPlaying.empty() || !fs::exists(g_nowPlaying)) { MessageBoxW(g_window, L"Load a track into Now Playing first.", L"ETHERCONTROL", MB_OK | MB_ICONINFORMATION); return; }
    etherbeat::ControlUiInput input;
    input.source_audio = g_nowPlaying;
    input.instruction = utf8(getText(g_controlPrompt));
    input.reference_strength = parseDouble(g_controlStrength, 0.80, 0.0, 1.0);
    input.edit_start_seconds = parseDouble(g_controlStart, 0.0, 0.0, 600.0);
    input.edit_end_seconds = parseDouble(g_controlEnd, 8.0, 0.0, 600.0);
    input.source_duration_seconds = g_nowAnalysis.ready ? g_nowAnalysis.duration_seconds : std::max(10.0, input.edit_end_seconds);
    input.bpm = parseDouble(g_bpm, 0.0, 0.0, 300.0);
    input.key = utf8(getText(g_key));

    etherbeat::GenerationRequest request;
    try { request = etherbeat::make_control_ui_request(action, input); }
    catch (const std::exception& e) { MessageBoxW(g_window, wide(e.what()).c_str(), L"ETHERCONTROL", MB_OK | MB_ICONINFORMATION); return; }
    if (g_working.exchange(true)) return;
    stopPlayback();
    const bool replace = action == etherbeat::ControlUiAction::ReplaceSection;
    setStatus(replace ? L"ETHERCONTROL // repainting selected section..." : L"ETHERCONTROL // generating controlled variation...");
    const auto source = g_nowPlaying;
    std::thread([request, source, replace] {
        try {
            auto router = etherbeat::make_default_router();
            auto output = libraryRoot() / L"control" / source.stem();
            std::error_code ec; fs::create_directories(output, ec);
            const auto artifact = router.generate(request, output);
            const auto analysis = etherbeat::analyze_audio_file(artifact.audio_path);
            if (!analysis.ready) throw std::runtime_error(analysis.error.empty() ? "Control render completed but audio analysis failed" : analysis.error);
            postWork(replace ? WorkKind::ControlReplace : WorkKind::ControlVariation, true, L"", artifact.audio_path, analysis, artifact.resolved_seed);
        } catch (const std::exception& e) { postWork(replace ? WorkKind::ControlReplace : WorkKind::ControlVariation, false, wide(e.what()), {}, {}, 0); }
    }).detach();
}

void startReferenceAnalysis(const fs::path& path) {
    if (g_working.exchange(true)) return;
    setStatus(L"SONG LAB // reading frequency DNA...");
    std::thread([path] {
        const auto analysis = etherbeat::analyze_audio_file(path);
        postWork(WorkKind::AnalyzeReference, analysis.ready, wide(analysis.error), path, analysis, 0);
    }).detach();
}

void startEngineCheck() {
    if (g_working.exchange(true)) return;
    setStatus(etherbeat::managed_ace_step_runtime_installed() ? L"ENGINE // starting private model..." : L"ENGINE // first-use runtime download may be large...");
    std::thread([] {
        try { etherbeat::ensure_managed_ace_step_engine(); postWork(WorkKind::EngineCheck, true, L"", {}, {}, 0); }
        catch (const std::exception& e) { postWork(WorkKind::EngineCheck, false, wide(e.what()), {}, {}, 0); }
    }).detach();
}

void drawButton(Graphics& g, const RectF& r, const std::wstring& label, int action, bool accent = false) {
    roundRect(g, r, 17.f, accent ? Color(255, 34, 29, 10) : Color(246, 15, 15, 16), accent ? amber(190) : Color(90, 94, 87, 70), 1.f);
    drawText(g, label, r, 11.f, accent ? amber() : warm(), FontStyleBold, StringAlignmentCenter, StringAlignmentCenter);
    addHit(r, action);
}

void drawNav(Graphics& g, float width) {
    struct Tab { const wchar_t* label; Screen screen; int action; } tabs[] = {
        {L"home", Screen::Home, ActTabHome}, {L"create", Screen::Create, ActTabCreate},
        {L"library", Screen::Library, ActTabLibrary}, {L"now playing", Screen::NowPlaying, ActTabNowPlaying},
        {L"song lab", Screen::SongLab, ActTabSongLab}, {L"engine", Screen::Engine, ActTabEngine}
    };
    float x = 34.f;
    for (const auto& tab : tabs) {
        const float w = std::max(76.f, 22.f + static_cast<float>(wcslen(tab.label)) * 7.3f);
        const RectF r = R(x, 73.f, w, 30.f);
        const bool selected = g_screen == tab.screen;
        if (selected) roundRect(g, r, 14.f, Color(255, 31, 27, 10), amber(185), 1.f);
        drawText(g, tab.label, r, 11.f, selected ? amber() : muted(), selected ? FontStyleBold : FontStyleRegular, StringAlignmentCenter, StringAlignmentCenter);
        addHit(r, tab.action); x += w + 7.f;
    }
    drawText(g, L"ETHERPLAY DNA // ETHERBEAT V0.2", R(width - 330.f, 78.f, 295.f, 20.f), 10.f, muted(), FontStyleBold, StringAlignmentFar);
}

void drawAnalyzer(Graphics& g, const RectF& area, const etherbeat::AudioAnalysis& analysis) {
    roundRect(g, area, 22.f, panel(), Color(95, 91, 84, 67));
    drawText(g, L"ETHERTECH // FREQUENCY ANALYZER", R(area.X + 20.f, area.Y + 18.f, area.Width - 40.f, 24.f), 11.f, amber(), FontStyleBold);
    const float gx = area.X + 20.f, gy = area.Y + 64.f, gw = area.Width - 40.f, gh = area.Height - 150.f;
    for (int line = 0; line <= 4; ++line) { const float y = gy + gh * static_cast<float>(line) / 4.f; Pen p(Color(34, 100, 94, 77)); g.DrawLine(&p, gx, y, gx + gw, y); }
    constexpr int bars = 32; const float gap = 3.f; const float bw = (gw - gap * (bars - 1)) / bars;
    for (int i = 0; i < bars; ++i) {
        const float value = analysis.ready ? analysis.spectrum[static_cast<std::size_t>(i)] : .025f;
        const float h = std::max(2.f, value * gh); const float x = gx + static_cast<float>(i) * (bw + gap);
        LinearGradientBrush fill(PointF(x, gy + gh), PointF(x, gy + gh - h), violet(170), pink(205));
        g.FillRectangle(&fill, x, gy + gh - h, std::max(1.f, bw), h);
    }
    std::wstring stats = analysis.ready ? L"BASS " + std::to_wstring(static_cast<int>(analysis.bass * 100.f)) + L"%   MID " + std::to_wstring(static_cast<int>(analysis.mid * 100.f)) + L"%   AIR " + std::to_wstring(static_cast<int>(analysis.treble * 100.f)) + L"%   BEAT " + std::to_wstring(static_cast<int>(analysis.beat_peak * 100.f)) + L"%" : L"NO AUDIO CAPTURED";
    drawText(g, stats, R(area.X + 20.f, area.GetBottom() - 62.f, area.Width - 40.f, 22.f), 10.f, analysis.ready ? warm() : muted(), FontStyleBold);
    if (analysis.ready) { std::wostringstream meta; meta << analysis.sample_rate << L" Hz // " << std::fixed << std::setprecision(1) << analysis.duration_seconds << L" sec"; drawText(g, meta.str(), R(area.X + 20.f, area.GetBottom() - 38.f, area.Width - 40.f, 20.f), 9.f, muted()); }
}

void drawHome(Graphics& g, float W, float H) {
    drawText(g, L"PRIVATE MUSIC MACHINE", R(42.f, 128.f, 550.f, 42.f), 29.f, warm(), FontStyleBold);
    drawText(g, L"EtherPlay's media brain evolved into generation, analysis, playback and private model control.", R(44.f, 174.f, W - 88.f, 28.f), 12.f, muted());
    const RectF left = R(42.f, 226.f, W * .46f, H - 300.f), right = R(left.GetRight() + 18.f, 226.f, W - left.GetRight() - 60.f, H - 300.f);
    roundRect(g, left, 24.f, panel(), Color(85, 98, 89, 70)); roundRect(g, right, 24.f, panel(), Color(85, 98, 89, 70));
    drawText(g, L"FOUNDATION", R(left.X + 22.f, left.Y + 22.f, left.Width - 44.f, 22.f), 11.f, amber(), FontStyleBold);
    drawText(g, L"ACE-Step 1.5 // PRETRAINED", R(left.X + 22.f, left.Y + 60.f, left.Width - 44.f, 34.f), 19.f, warm(), FontStyleBold);
    drawText(g, L"Search, measure, compare and branch locally. Pleiadian taste training remains optional and later.", R(left.X + 22.f, left.Y + 106.f, left.Width - 44.f, 70.f), 12.f, muted());
    drawButton(g, R(left.X + 22.f, left.GetBottom() - 62.f, 150.f, 40.f), L"CREATE BEAT", ActTabCreate, true);
    const bool installed = etherbeat::managed_ace_step_runtime_installed(), ready = etherbeat::managed_ace_step_engine_ready();
    drawText(g, L"ENGINE STATE", R(right.X + 22.f, right.Y + 22.f, right.Width - 44.f, 22.f), 11.f, amber(), FontStyleBold);
    drawText(g, ready ? L"ONLINE" : (installed ? L"INSTALLED / SLEEPING" : L"NOT PROVISIONED YET"), R(right.X + 22.f, right.Y + 60.f, right.Width - 44.f, 34.f), 18.f, ready ? Color(255, 176, 228, 156) : warm(), FontStyleBold);
    drawButton(g, R(right.X + 22.f, right.GetBottom() - 62.f, 150.f, 40.f), L"ENGINE CHECK", ActStartEngine, true);
}

void drawCreate(Graphics& g, float W, float H) {
    drawText(g, L"CREATE", R(42.f, 126.f, 250.f, 38.f), 27.f, warm(), FontStyleBold);
    drawText(g, L"text -> 4 drafts -> EtherDNA -> critic ranking -> promoted winner", R(44.f, 166.f, 650.f, 24.f), 11.f, muted());
    roundRect(g, R(42.f, 208.f, W - 84.f, H - 278.f), 24.f, panel(), Color(85, 98, 89, 70));
    drawText(g, L"SYNESTHESIA / PRODUCTION LANGUAGE", R(66.f, 226.f, 430.f, 22.f), 10.f, amber(), FontStyleBold);
    drawText(g, L"BPM", R(66.f, 492.f, 90.f, 18.f), 10.f, muted(), FontStyleBold); drawText(g, L"KEY", R(190.f, 492.f, 90.f, 18.f), 10.f, muted(), FontStyleBold);
    drawText(g, L"DURATION", R(314.f, 492.f, 100.f, 18.f), 10.f, muted(), FontStyleBold); drawText(g, L"SEED", R(438.f, 492.f, 100.f, 18.f), 10.f, muted(), FontStyleBold);
    drawButton(g, R(66.f, H - 128.f, 210.f, 52.f), g_working ? L"SEARCHING..." : L"SEARCH x4", ActGenerate, true);
    drawText(g, L"EtherSearch keeps all candidates locally and promotes the highest measured match", R(296.f, H - 118.f, W - 370.f, 34.f), 10.f, muted());
}

void drawLibrary(Graphics& g, float W, float H) {
    drawText(g, L"LIBRARY", R(42.f, 126.f, 250.f, 38.f), 27.f, warm(), FontStyleBold);
    drawText(g, L"private local generations + control versions", R(44.f, 166.f, 390.f, 22.f), 11.f, muted());
    drawButton(g, R(W - 200.f, 132.f, 150.f, 38.f), L"OPEN FOLDER", ActOpenLibrary);
    const float x = 42.f, y0 = 214.f, w = W - 84.f;
    if (g_library.empty()) { roundRect(g, R(x, y0, w, 140.f), 22.f, panel(), Color(80, 92, 84, 68)); drawText(g, L"NO GENERATIONS YET", R(x + 22.f, y0 + 28.f, w - 44.f, 28.f), 15.f, warm(), FontStyleBold); return; }
    const std::size_t count = std::min<std::size_t>(8, g_library.size());
    for (std::size_t i = 0; i < count; ++i) {
        const float y = y0 + static_cast<float>(i) * 58.f; RectF row = R(x, y, w, 48.f); roundRect(g, row, 15.f, Color(242, 10, 10, 11), Color(65, 88, 82, 68));
        drawText(g, g_library[i].filename().wstring(), R(x + 18.f, y + 7.f, w - 170.f, 22.f), 11.f, warm(), FontStyleBold); drawText(g, L"click to load into now playing", R(x + 18.f, y + 27.f, w - 170.f, 16.f), 9.f, muted());
        addHit(row, ActLibraryBase + static_cast<int>(i));
    }
}

std::wstring signedPercent(float value) {
    const int p = static_cast<int>(std::round(value * 100.0f));
    return std::wstring(p >= 0 ? L"+" : L"") + std::to_wstring(p);
}

void drawVersionCompareRail(Graphics& g, float W, float H) {
    const RectF rail = R(42.f, H - 196.f, W - 84.f, 84.f);
    roundRect(g, rail, 18.f, Color(248, 8, 8, 9), Color(96, 113, 92, 57));
    bool isHead = g_versionLineage && g_versionLineage->current_is_promoted();
    std::wstring title = g_versionLineage ? (isHead ? L"ETHERVERSIONS // HEAD" : L"ETHERVERSIONS // BRANCH") : L"ETHERVERSIONS // UNTRACKED";
    std::wstring detail = g_versionLineage ? wide(g_versionLineage->current.operation) + L" // root " + wide(g_versionLineage->current.root_id.substr(0, 8)) : L"no lineage";
    drawText(g, title, R(rail.X + 16.f, rail.Y + 7.f, 210.f, 16.f), 9.f, isHead ? amber() : warm(), FontStyleBold);
    drawText(g, detail, R(rail.X + 16.f, rail.Y + 24.f, 220.f, 14.f), 8.f, muted());

    float x = rail.X + 240.f; const float y = rail.Y + 6.f;
    drawButton(g, R(x, y, 82.f, 30.f), L"PARENT", ActVersionParent); x += 88.f;
    drawButton(g, R(x, y, 94.f, 30.f), L"< CHILD", ActVersionPrevChild); x += 100.f;
    drawButton(g, R(x, y, 94.f, 30.f), L"CHILD >", ActVersionNextChild); x += 100.f;
    drawButton(g, R(x, y, 72.f, 30.f), L"HEAD", ActVersionHead, isHead); x += 78.f;
    drawButton(g, R(x, y, 92.f, 30.f), L"PROMOTE", ActVersionPromote, !isHead);

    const float cy = rail.Y + 44.f;
    std::wstring compareText = L"ETHERCOMPARE // no alternate branch yet";
    if (g_compareResult) {
        const auto& c = *g_compareResult;
        compareText = L"B=" + g_compareRelationship + L" // DNA " + std::to_wstring(static_cast<int>(std::round(c.similarity * 100.f))) +
            L"% // dBASS " + signedPercent(c.delta.low_end_weight) +
            L" // dBRIGHT " + signedPercent(c.delta.brightness) +
            L" // dRHYTHM " + signedPercent(c.delta.rhythmic_activity) +
            L" // dENERGY " + signedPercent(c.delta.energy);
    }
    drawText(g, compareText, R(rail.X + 16.f, cy + 5.f, rail.Width - 430.f, 24.f), 8.f, g_compareResult ? warm() : muted(), FontStyleBold);
    const float bx = rail.GetRight() - 390.f;
    drawButton(g, R(bx, cy, 76.f, 30.f), L"A", ActCompareA, !g_compareAuditionB);
    drawButton(g, R(bx + 82.f, cy, 76.f, 30.f), L"B", ActCompareB, g_compareAuditionB);
    drawButton(g, R(bx + 164.f, cy, 210.f, 30.f), L"A/B SYNCHRONIZED", ActCompareToggle, true);
}

void drawNowPlaying(Graphics& g, float W, float H) {
    drawText(g, L"NOW PLAYING", R(42.f, 126.f, 330.f, 38.f), 27.f, warm(), FontStyleBold);
    if (g_nowPlaying.empty()) { drawText(g, L"nothing loaded", R(44.f, 174.f, 400.f, 26.f), 13.f, muted()); drawButton(g, R(42.f, 224.f, 160.f, 42.f), L"OPEN LIBRARY", ActTabLibrary, true); return; }
    drawText(g, g_nowPlaying.filename().wstring(), R(44.f, 170.f, W - 88.f, 28.f), 14.f, amber(), FontStyleBold);
    const float contentW = W - 84.f, gap = 18.f, analyzerW = contentW * .58f;
    const RectF analyzer = R(42.f, 220.f, analyzerW, H - 430.f), control = R(analyzer.GetRight() + gap, 220.f, contentW - analyzerW - gap, H - 430.f);
    drawAnalyzer(g, analyzer, g_nowAnalysis);
    roundRect(g, control, 22.f, panel(), Color(104, 108, 87, 58));
    drawText(g, L"ETHERCONTROL // LIVE", R(control.X + 20.f, control.Y + 18.f, control.Width - 40.f, 22.f), 11.f, amber(), FontStyleBold);
    drawText(g, L"producer instruction", R(control.X + 20.f, control.Y + 48.f, control.Width - 40.f, 18.f), 9.f, muted(), FontStyleBold);
    drawText(g, L"REFERENCE STRENGTH", R(control.X + 20.f, control.Y + 190.f, control.Width - 40.f, 18.f), 9.f, muted(), FontStyleBold);
    drawText(g, L"REPAINT WINDOW", R(control.X + 20.f, control.Y + 248.f, control.Width - 40.f, 18.f), 9.f, muted(), FontStyleBold);
    drawText(g, L"START", R(control.X + 20.f, control.Y + 270.f, 80.f, 16.f), 8.f, muted(), FontStyleBold); drawText(g, L"END", R(control.X + 132.f, control.Y + 270.f, 80.f, 16.f), 8.f, muted(), FontStyleBold);
    const float buttonY = control.GetBottom() - 52.f, buttonW = (control.Width - 52.f) * .5f;
    drawButton(g, R(control.X + 18.f, buttonY, buttonW, 38.f), g_working ? L"WORKING..." : L"VARIATION", ActVariation, true);
    drawButton(g, R(control.X + 34.f + buttonW, buttonY, buttonW, 38.f), g_working ? L"WORKING..." : L"REPLACE SECTION", ActReplaceSection);
    drawVersionCompareRail(g, W, H);
    drawButton(g, R(42.f, H - 104.f, 120.f, 42.f), L"PLAY", ActPlay, true); drawButton(g, R(174.f, H - 104.f, 120.f, 42.f), L"STOP", ActStop); drawButton(g, R(306.f, H - 104.f, 150.f, 42.f), L"OPEN LIBRARY", ActOpenLibrary);
    drawText(g, g_compareAuditionB ? L"Auditioning B at the same timeline position. Selection A remains unchanged." : L"A is the selected version. Compare B never changes HEAD by itself.", R(480.f, H - 94.f, W - 520.f, 26.f), 9.f, muted());
}

void drawSongLab(Graphics& g, float W, float H) {
    drawText(g, L"SONG LAB", R(42.f, 126.f, 300.f, 38.f), 27.f, warm(), FontStyleBold); drawText(g, L"reference audio -> EtherPlay FFT -> measurable sound DNA", R(44.f, 166.f, 560.f, 22.f), 11.f, muted());
    drawButton(g, R(W - 224.f, 132.f, 174.f, 38.f), L"CHOOSE AUDIO", ActChooseReference, true); if (!g_reference.empty()) drawText(g, g_reference.filename().wstring(), R(44.f, 194.f, W - 88.f, 24.f), 11.f, amber(), FontStyleBold);
    drawAnalyzer(g, R(42.f, 230.f, W - 84.f, H - 304.f), g_reference.empty() ? etherbeat::AudioAnalysis{} : g_referenceAnalysis);
}

void drawEngine(Graphics& g, float W, float H) {
    const bool installed = etherbeat::managed_ace_step_runtime_installed(), ready = etherbeat::managed_ace_step_engine_ready();
    drawText(g, L"ENGINE", R(42.f, 126.f, 260.f, 38.f), 27.f, warm(), FontStyleBold); drawText(g, L"model health + local runtime diagnostics", R(44.f, 166.f, 430.f, 22.f), 11.f, muted());
    RectF card = R(42.f, 216.f, W - 84.f, H - 286.f); roundRect(g, card, 24.f, panel(), Color(88, 98, 89, 70));
    drawText(g, L"FOUNDATION MODEL", R(card.X + 24.f, card.Y + 24.f, 260.f, 20.f), 10.f, amber(), FontStyleBold); drawText(g, L"ACE-Step 1.5 // PRETRAINED", R(card.X + 24.f, card.Y + 54.f, 430.f, 32.f), 19.f, warm(), FontStyleBold);
    drawText(g, L"PRODUCER STACK", R(card.X + 24.f, card.Y + 112.f, 260.f, 20.f), 10.f, amber(), FontStyleBold); drawText(g, L"SEARCH + CONTROL + VERSIONS + COMPARE", R(card.X + 24.f, card.Y + 142.f, 520.f, 28.f), 16.f, warm(), FontStyleBold);
    drawText(g, L"EtherCompare now measures branch DNA and performs synchronized A/B playback without moving version HEAD.", R(card.X + 24.f, card.Y + 182.f, card.Width * .54f, 70.f), 11.f, muted());
    const float sx = card.X + card.Width * .62f; drawText(g, L"RUNTIME", R(sx, card.Y + 24.f, 160.f, 20.f), 10.f, amber(), FontStyleBold); drawText(g, installed ? L"INSTALLED" : L"MISSING", R(sx, card.Y + 54.f, 220.f, 30.f), 17.f, installed ? warm() : Color(255, 235, 130, 120), FontStyleBold); drawText(g, L"API", R(sx, card.Y + 112.f, 160.f, 20.f), 10.f, amber(), FontStyleBold); drawText(g, ready ? L"ONLINE" : L"OFFLINE", R(sx, card.Y + 142.f, 220.f, 30.f), 17.f, ready ? Color(255, 176, 228, 156) : muted(), FontStyleBold);
    drawButton(g, R(card.X + 24.f, card.GetBottom() - 62.f, 170.f, 40.f), L"START / CHECK", ActStartEngine, true); drawButton(g, R(card.X + 208.f, card.GetBottom() - 62.f, 170.f, 40.f), L"OPEN RUNTIME", ActOpenRuntime);
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); Graphics g(dc); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    RECT client{}; GetClientRect(hwnd, &client); const float W = static_cast<float>(client.right), H = static_cast<float>(client.bottom);
    g_hits.clear(); LinearGradientBrush bg(PointF(0.f, 0.f), PointF(W, H), Color(255, 3, 3, 4), Color(255, 13, 11, 8)); g.FillRectangle(&bg, 0.f, 0.f, W, H);
    drawText(g, L"ETHERBEAT", R(34.f, 22.f, 300.f, 34.f), 25.f, warm(), FontStyleBold); drawText(g, L"ALIEN WORKSHOP // LOCAL", R(36.f, 52.f, 280.f, 18.f), 9.f, muted(), FontStyleBold); drawNav(g, W);
    switch (g_screen) { case Screen::Home: drawHome(g, W, H); break; case Screen::Create: drawCreate(g, W, H); break; case Screen::Library: drawLibrary(g, W, H); break; case Screen::NowPlaying: drawNowPlaying(g, W, H); break; case Screen::SongLab: drawSongLab(g, W, H); break; case Screen::Engine: drawEngine(g, W, H); break; }
    drawText(g, g_status, R(36.f, H - 31.f, W - 72.f, 18.f), 9.f, muted()); EndPaint(hwnd, &ps);
}

void layoutEdits() {
    if (!g_window) return; RECT r{}; GetClientRect(g_window, &r); const int W = r.right;
    MoveWindow(g_prompt, 66, 256, std::max(420, W - 132), 210, TRUE); MoveWindow(g_bpm, 66, 516, 106, 32, TRUE); MoveWindow(g_key, 190, 516, 106, 32, TRUE); MoveWindow(g_duration, 314, 516, 106, 32, TRUE); MoveWindow(g_seed, 438, 516, 138, 32, TRUE);
    const int contentW = W - 84, analyzerW = static_cast<int>(static_cast<double>(contentW) * .58), controlX = 42 + analyzerW + 18, controlW = std::max(310, contentW - analyzerW - 18);
    MoveWindow(g_controlPrompt, controlX + 20, 292, controlW - 40, 104, TRUE); MoveWindow(g_controlStrength, controlX + 20, 432, 116, 30, TRUE); MoveWindow(g_controlStart, controlX + 20, 510, 96, 30, TRUE); MoveWindow(g_controlEnd, controlX + 132, 510, 96, 30, TRUE);
}

HWND makeEdit(HWND parent, int id, const wchar_t* initial, DWORD extra = 0) {
    HWND h = CreateWindowExW(0, L"EDIT", initial, WS_CHILD | WS_TABSTOP | ES_LEFT | extra, 0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(extra & ES_MULTILINE ? g_promptFont : g_uiFont), TRUE); return h;
}

void createControls(HWND hwnd) {
    g_prompt = makeEdit(hwnd, ID_PROMPT, L"haunted 2016 cloud-rap instrumental, enormous negative space, cold purple chrome, submerged bass, beautifully degraded, lonely but arrogant", ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN);
    g_bpm = makeEdit(hwnd, ID_BPM, L"68"); g_key = makeEdit(hwnd, ID_KEY, L"F# minor"); g_duration = makeEdit(hwnd, ID_DURATION, L"20"); g_seed = makeEdit(hwnd, ID_SEED, L"random");
    g_controlPrompt = makeEdit(hwnd, ID_CONTROL_PROMPT, L"make this version colder, stranger and more degraded while preserving the musical identity", ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN);
    g_controlStrength = makeEdit(hwnd, ID_CONTROL_STRENGTH, L"0.80"); g_controlStart = makeEdit(hwnd, ID_CONTROL_START, L"0.00"); g_controlEnd = makeEdit(hwnd, ID_CONTROL_END, L"8.00");
    layoutEdits(); showCreateControls(false); showControlControls(false);
}

void handleAction(int action) {
    if (action >= ActLibraryBase && action < ActLibraryBase + 100) {
        const std::size_t index = static_cast<std::size_t>(action - ActLibraryBase);
        if (index < g_library.size()) { loadNowPlaying(g_library[index], false); setStatus(L"LIBRARY // " + g_nowPlaying.filename().wstring()); }
        return;
    }
    switch (action) {
    case ActTabHome: setScreen(Screen::Home); break; case ActTabCreate: setScreen(Screen::Create); break; case ActTabLibrary: setScreen(Screen::Library); break; case ActTabNowPlaying: setScreen(Screen::NowPlaying); break; case ActTabSongLab: setScreen(Screen::SongLab); break; case ActTabEngine: setScreen(Screen::Engine); break;
    case ActGenerate: startGenerate(); break; case ActVariation: startControl(etherbeat::ControlUiAction::Variation); break; case ActReplaceSection: startControl(etherbeat::ControlUiAction::ReplaceSection); break;
    case ActVersionParent: navigateVersionParent(); break; case ActVersionPrevChild: navigateVersionBranch(-1); break; case ActVersionNextChild: navigateVersionBranch(1); break; case ActVersionHead: navigateVersionHead(); break; case ActVersionPromote: promoteCurrentVersion(); break;
    case ActCompareA: auditionCompare(false); break; case ActCompareB: auditionCompare(true); break; case ActCompareToggle: toggleCompare(); break;
    case ActChooseReference: { const auto chosen = chooseAudio(g_window); if (!chosen.empty()) { g_reference = fs::path(chosen); startReferenceAnalysis(g_reference); } break; }
    case ActOpenLibrary: { const auto root = libraryRoot(); ShellExecuteW(g_window, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break; }
    case ActPlay: playPathAt(activeAuditionPath(), playbackPositionMs(), true); break; case ActStop: stopPlayback(); break; case ActStartEngine: startEngineCheck(); break;
    case ActOpenRuntime: { const auto root = etherbeat::managed_ace_step_runtime_root(); std::error_code ec; fs::create_directories(root, ec); ShellExecuteW(g_window, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break; }
    default: break;
    }
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: g_window = hwnd; createControls(hwnd); refreshLibrary(); return 0;
    case WM_GETMINMAXINFO: { auto* info = reinterpret_cast<MINMAXINFO*>(lParam); info->ptMinTrackSize.x = kMinWidth; info->ptMinTrackSize.y = kMinHeight; return 0; }
    case WM_SIZE: layoutEdits(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_LBUTTONUP: { const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam); for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it) if (hit(it->rect, x, y)) { handleAction(it->action); break; } return 0; }
    case WM_CTLCOLOREDIT: { HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, RGB(244, 242, 235)); SetBkColor(dc, RGB(12, 12, 13)); return reinterpret_cast<LRESULT>(g_editBrush); }
    case WM_APP_WORK_DONE: {
        WorkKind kind; bool success; std::wstring error; fs::path artifact; etherbeat::AudioAnalysis analysis; std::uint64_t seed;
        { std::scoped_lock lock(g_resultMutex); kind = g_pendingKind; success = g_pendingSuccess; error = g_pendingError; artifact = g_pendingArtifact; analysis = g_pendingAnalysis; seed = g_pendingSeed; }
        g_working = false;
        if (!success) { setStatus(L"FAILED // " + error); if (kind == WorkKind::Generate || kind == WorkKind::EngineCheck) setScreen(Screen::Engine); else if (kind == WorkKind::ControlVariation || kind == WorkKind::ControlReplace) setScreen(Screen::NowPlaying); MessageBoxW(hwnd, error.c_str(), L"ETHERBEAT ENGINE", MB_OK | MB_ICONERROR); return 0; }
        if (kind == WorkKind::Generate) { g_nowPlaying = artifact; g_nowAnalysis = analysis; SetWindowTextW(g_seed, std::to_wstring(seed).c_str()); syncControlWindowToTrack(); refreshVersionState(); refreshCompareState(); refreshLibrary(); setStatus(L"ETHERSEARCH WINNER // " + artifact.filename().wstring()); setScreen(Screen::NowPlaying); playPath(g_nowPlaying); }
        else if (kind == WorkKind::ControlVariation || kind == WorkKind::ControlReplace) { g_nowPlaying = artifact; g_nowAnalysis = analysis; syncControlWindowToTrack(); refreshVersionState(); refreshCompareState(); refreshLibrary(); setStatus((kind == WorkKind::ControlReplace ? L"REPAINT BRANCH // " : L"VARIATION BRANCH // ") + artifact.filename().wstring()); setScreen(Screen::NowPlaying); playPath(g_nowPlaying); }
        else if (kind == WorkKind::AnalyzeReference) { g_referenceAnalysis = analysis; setStatus(L"SONG LAB // REFERENCE HEARD"); setScreen(Screen::SongLab); }
        else if (kind == WorkKind::EngineCheck) { setStatus(L"ENGINE // PRETRAINED MODEL API ONLINE"); setScreen(Screen::Engine); }
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(hwnd); return 0;
    case WM_DESTROY: stopPlayback(); etherbeat::shutdown_managed_ace_step_engine(); PostQuitMessage(0); return 0;
    default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_instance = instance;
    GdiplusStartupInput input; if (GdiplusStartup(&g_gdiplus, &input, nullptr) != Ok) return 1;
    g_uiFont = CreateFontW(-16, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_promptFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_editBrush = CreateSolidBrush(RGB(12, 12, 13));
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = windowProc; wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wc.lpszClassName = kWindowClass; if (!RegisterClassExW(&wc)) return 1;
    HWND hwnd = CreateWindowExW(0, kWindowClass, L"ETHERBEAT // Alien Workshop V0.2N", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 900, nullptr, nullptr, instance, nullptr); if (!hwnd) return 1;
    BOOL dark = TRUE; DwmSetWindowAttribute(hwnd, kImmersiveDarkModeAttribute, &dark, sizeof(dark)); ShowWindow(hwnd, showCommand); UpdateWindow(hwnd);
    MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (g_editBrush) DeleteObject(g_editBrush); if (g_uiFont) DeleteObject(g_uiFont); if (g_promptFont) DeleteObject(g_promptFont); GdiplusShutdown(g_gdiplus); return static_cast<int>(message.wParam);
}
