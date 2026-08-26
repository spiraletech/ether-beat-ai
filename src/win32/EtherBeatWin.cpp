#include "etherbeat/GenerationTypes.hpp"
#include "etherbeat/ModelRouter.hpp"

#include <windows.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"EtherBeatMainWindow";
constexpr int kPromptId = 1001;
constexpr int kGenerateId = 1002;
constexpr int kStatusId = 1003;

HWND g_prompt = nullptr;
HWND g_status = nullptr;

std::wstring get_window_text(HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1u, L'\0');
    GetWindowTextW(window, buffer.data(), static_cast<int>(buffer.size()));
    return std::wstring{buffer.data()};
}

std::string to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);

    if (size <= 0) {
        throw std::runtime_error("Could not encode prompt as UTF-8");
    }

    std::string output(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        output.data(), size, nullptr, nullptr);
    return output;
}

void set_status(const std::wstring& text) {
    SetWindowTextW(g_status, text.c_str());
}

void generate_from_prompt(HWND owner) {
    try {
        const auto prompt_wide = get_window_text(g_prompt);
        const auto prompt = to_utf8(prompt_wide);

        if (prompt.empty()) {
            MessageBoxW(owner, L"Enter a generation prompt first.", L"ETHERBEAT", MB_OK | MB_ICONINFORMATION);
            return;
        }

        set_status(L"Generating prototype artifact...");

        etherbeat::GenerationRequest request;
        request.prompt = prompt;
        request.duration_seconds = 10.0;

        etherbeat::ModelRouter router{etherbeat::make_default_backend()};
        const auto artifact = router.generate(request, L"generated");

        const auto absolute_audio = std::filesystem::absolute(artifact.audio_path).wstring();
        set_status(L"Created: " + absolute_audio);

        MessageBoxW(
            owner,
            (L"Prototype artifact created.\n\n" + absolute_audio +
             L"\n\nThe current backend intentionally emits silence while the real AI backend is integrated.").c_str(),
            L"ETHERBEAT V0.1",
            MB_OK | MB_ICONINFORMATION);
    } catch (const std::exception& error) {
        std::wstring message = L"Generation failed.";

        const std::string narrow = error.what();
        if (!narrow.empty()) {
            const int required = MultiByteToWideChar(
                CP_UTF8, 0, narrow.data(), static_cast<int>(narrow.size()), nullptr, 0);
            if (required > 0) {
                std::wstring detail(static_cast<std::size_t>(required), L'\0');
                MultiByteToWideChar(
                    CP_UTF8, 0, narrow.data(), static_cast<int>(narrow.size()),
                    detail.data(), required);
                message += L"\n\n" + detail;
            }
        }

        set_status(L"Generation failed.");
        MessageBoxW(owner, message.c_str(), L"ETHERBEAT ERROR", MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_CREATE: {
        CreateWindowW(
            L"STATIC", L"ETHERBEAT // PRIVATE GENERATIVE WORKSTATION",
            WS_CHILD | WS_VISIBLE,
            24, 20, 620, 24,
            window, nullptr, nullptr, nullptr);

        CreateWindowW(
            L"STATIC", L"Describe the sound, emotion, environment, or production behavior:",
            WS_CHILD | WS_VISIBLE,
            24, 58, 620, 22,
            window, nullptr, nullptr, nullptr);

        g_prompt = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"haunted private pleiadian instrumental, enormous negative space, alien chrome loneliness",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            24, 84, 700, 150,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPromptId)),
            nullptr,
            nullptr);

        CreateWindowW(
            L"BUTTON", L"GENERATE PROTOTYPE",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            24, 252, 220, 42,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGenerateId)),
            nullptr,
            nullptr);

        g_status = CreateWindowW(
            L"STATIC",
            L"Backend: mock-wave-48k // model bridge not connected yet",
            WS_CHILD | WS_VISIBLE,
            24, 314, 700, 42,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusId)),
            nullptr,
            nullptr);

        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(w_param) == kGenerateId && HIWORD(w_param) == BN_CLICKED) {
            generate_from_prompt(window);
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&window_class)) {
        MessageBoxW(nullptr, L"Could not register ETHERBEAT window class.", L"ETHERBEAT", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        kWindowClass,
        L"ETHERBEAT V0.1 // Alien Workshop",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        780, 430,
        nullptr, nullptr, instance, nullptr);

    if (!window) {
        MessageBoxW(nullptr, L"Could not create ETHERBEAT window.", L"ETHERBEAT", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
