#include "etherbeat/AceStepEngineManager.hpp"

#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace etherbeat {
namespace {

namespace fs = std::filesystem;

constexpr wchar_t kPortableRuntimeUrl[] =
    L"https://files.acemusic.ai/acemusic/win/ACE-Step-1.5.7z";
constexpr wchar_t kHost[] = L"127.0.0.1";
constexpr INTERNET_PORT kPort = 8001;

std::mutex g_engineMutex;
HANDLE g_engineProcess = nullptr;
HANDLE g_engineJob = nullptr;

class InternetHandle {
public:
    explicit InternetHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~InternetHandle() { if (handle_) WinHttpCloseHandle(handle_); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    [[nodiscard]] HINTERNET get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
private:
    HINTERNET handle_{};
};

struct RuntimePaths {
    fs::path root;
    fs::path python;
    fs::path apiScript;
};

fs::path executable_directory() {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("EtherBeat could not resolve its executable directory");
    }
    return fs::path(std::wstring(buffer.data(), length)).parent_path();
}

fs::path local_appdata_directory() {
    PWSTR raw = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw);
    if (FAILED(hr) || !raw) {
        throw std::runtime_error("EtherBeat could not resolve Local AppData");
    }
    fs::path result(raw);
    CoTaskMemFree(raw);
    return result;
}

fs::path preferred_runtime_root() {
    return local_appdata_directory() / L"EtherTech" / L"EtherBeat" / L"runtime";
}

bool path_is_runtime(const fs::path& root, RuntimePaths& out) {
    const fs::path api = root / L"acestep" / L"api_server.py";
    if (!fs::exists(api)) return false;

    const fs::path embedded = root / L"python_embedded" / L"python.exe";
    if (fs::exists(embedded)) {
        out = RuntimePaths{root, embedded, api};
        return true;
    }

    const fs::path venv = root / L".venv" / L"Scripts" / L"python.exe";
    if (fs::exists(venv)) {
        out = RuntimePaths{root, venv, api};
        return true;
    }

    return false;
}

bool find_runtime(RuntimePaths& out) {
    std::vector<fs::path> candidates;
    try {
        const auto managed = preferred_runtime_root();
        candidates.push_back(managed / L"ACE-Step-1.5");
        candidates.push_back(managed);
    } catch (...) {
    }

    try {
        const auto legacy = executable_directory() / L"runtime";
        candidates.push_back(legacy / L"ACE-Step-1.5");
        candidates.push_back(legacy);
    } catch (...) {
    }

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!fs::exists(candidate, ec) || ec) continue;
        if (path_is_runtime(candidate, out)) return true;
    }

    // Portable archives can occasionally add an extra top-level directory.
    // Search only the managed runtime root as a recovery path.
    try {
        const auto managed = preferred_runtime_root();
        std::error_code ec;
        if (!fs::exists(managed, ec) || ec) return false;

        for (fs::recursive_directory_iterator it(
                 managed,
                 fs::directory_options::skip_permission_denied,
                 ec), end;
             it != end && !ec;
             it.increment(ec)) {

            if (!it->is_regular_file(ec) || ec) continue;
            if (it->path().filename() != L"api_server.py") continue;
            if (it->path().parent_path().filename() != L"acestep") continue;

            const fs::path root = it->path().parent_path().parent_path();
            if (path_is_runtime(root, out)) return true;
        }
    } catch (...) {
    }

    return false;
}

bool quick_health_check() noexcept {
    try {
        InternetHandle session{WinHttpOpen(
            L"ETHERBEAT/0.1C",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0)};
        if (!session) return false;
        WinHttpSetTimeouts(session.get(), 1000, 1000, 1000, 1000);

        InternetHandle connection{WinHttpConnect(session.get(), kHost, kPort, 0)};
        if (!connection) return false;

        InternetHandle request{WinHttpOpenRequest(
            connection.get(), L"GET", L"/health", nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)};
        if (!request) return false;

        if (!WinHttpSendRequest(
                request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            return false;
        }
        if (!WinHttpReceiveResponse(request.get(), nullptr)) return false;

        DWORD status = 0;
        DWORD size = sizeof(status);
        if (!WinHttpQueryHeaders(
                request.get(),
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &status,
                &size,
                WINHTTP_NO_HEADER_INDEX)) {
            return false;
        }
        return status >= 200 && status < 300;
    } catch (...) {
        return false;
    }
}

void download_portable_runtime(const fs::path& archive) {
    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(kPortableRuntimeUrl, 0, 0, &components)) {
        throw std::runtime_error("EtherBeat could not parse the ACE-Step runtime URL");
    }

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }

    InternetHandle session{WinHttpOpen(
        L"ETHERBEAT/0.1C",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0)};
    if (!session) throw std::runtime_error("EtherBeat could not open its runtime downloader");

    WinHttpSetTimeouts(session.get(), 10000, 10000, 30000, 300000);
    InternetHandle connection{WinHttpConnect(session.get(), host.c_str(), components.nPort, 0)};
    if (!connection) throw std::runtime_error("EtherBeat could not reach the ACE-Step runtime host");

    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle request{WinHttpOpenRequest(
        connection.get(), L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
    if (!request) throw std::runtime_error("EtherBeat could not create the runtime download request");

    if (!WinHttpSendRequest(
            request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        throw std::runtime_error("EtherBeat could not download the local AI runtime");
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(
        request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) {
        throw std::runtime_error("ACE-Step portable runtime download returned an HTTP error");
    }

    fs::create_directories(archive.parent_path());
    const fs::path partial = archive.wstring() + L".part";
    std::ofstream output(partial, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("EtherBeat could not create its runtime archive");

    std::vector<std::uint8_t> buffer(1024 * 1024);
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            throw std::runtime_error("Runtime download was interrupted");
        }
        if (available == 0) break;

        while (available > 0) {
            const DWORD chunk = static_cast<DWORD>(
                (std::min)(static_cast<std::size_t>(available), buffer.size()));
            DWORD read = 0;
            if (!WinHttpReadData(request.get(), buffer.data(), chunk, &read)) {
                throw std::runtime_error("Runtime download failed while reading data");
            }
            if (read == 0) break;
            output.write(reinterpret_cast<const char*>(buffer.data()), read);
            if (!output) throw std::runtime_error("Runtime download failed while writing data");
            available -= read;
        }
    }
    output.close();

    std::error_code ec;
    fs::remove(archive, ec);
    fs::rename(partial, archive, ec);
    if (ec) {
        throw std::runtime_error("EtherBeat could not finalize the downloaded runtime archive");
    }
}

DWORD run_hidden_and_wait(std::wstring command, const fs::path& workingDirectory) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            nullptr,
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
            &startup,
            &process)) {
        throw std::runtime_error("EtherBeat could not start the Windows runtime extractor");
    }

    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    return exitCode;
}

void provision_runtime_if_needed() {
    RuntimePaths runtime;
    if (find_runtime(runtime)) return;

    const fs::path root = preferred_runtime_root();
    fs::create_directories(root);
    const fs::path archive = root / L"ACE-Step-1.5.7z";

    if (!fs::exists(archive)) {
        download_portable_runtime(archive);
    }

    std::wstring command = L"tar.exe -xf \"" + archive.wstring() +
                           L"\" -C \"" + root.wstring() + L"\"";
    const DWORD extractionExit = run_hidden_and_wait(command, root);
    if (extractionExit != 0) {
        throw std::runtime_error(
            "EtherBeat downloaded the local AI runtime but Windows could not extract it. "
            "This build requires the built-in Windows tar.exe extractor.");
    }

    if (!find_runtime(runtime)) {
        throw std::runtime_error(
            "EtherBeat extracted ACE-Step but could not locate its embedded Python runtime");
    }

    std::error_code ec;
    fs::remove(archive, ec);
}

void close_managed_handles() noexcept {
    if (g_engineJob) {
        CloseHandle(g_engineJob); // KILL_ON_JOB_CLOSE terminates the entire child tree.
        g_engineJob = nullptr;
    }
    if (g_engineProcess) {
        CloseHandle(g_engineProcess);
        g_engineProcess = nullptr;
    }
}

void start_managed_server() {
    if (quick_health_check()) return;

    RuntimePaths runtime;
    if (!find_runtime(runtime)) {
        throw std::runtime_error("EtherBeat local AI runtime is not installed correctly");
    }

    if (g_engineProcess) {
        DWORD exitCode = STILL_ACTIVE;
        if (!GetExitCodeProcess(g_engineProcess, &exitCode) || exitCode != STILL_ACTIVE) {
            close_managed_handles();
        }
    }

    if (!g_engineJob) {
        g_engineJob = CreateJobObjectW(nullptr, nullptr);
        if (!g_engineJob) throw std::runtime_error("EtherBeat could not create its engine process container");

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(
                g_engineJob,
                JobObjectExtendedLimitInformation,
                &limits,
                sizeof(limits))) {
            close_managed_handles();
            throw std::runtime_error("EtherBeat could not secure its engine process container");
        }
    }

    // ACE-Step's Windows launcher documents these environment switches.
    // NO_INIT makes the API come up quickly; model weights lazy-load on generation.
    SetEnvironmentVariableW(L"ACESTEP_NO_INIT", L"true");
    SetEnvironmentVariableW(L"ACESTEP_INIT_LLM", L"auto");

    std::wstring command = L"\"" + runtime.python.wstring() + L"\" \"" +
                           runtime.apiScript.wstring() +
                           L"\" --host 127.0.0.1 --port 8001";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            runtime.python.c_str(),
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
            nullptr,
            runtime.root.c_str(),
            &startup,
            &process)) {
        close_managed_handles();
        throw std::runtime_error("EtherBeat could not launch its private ACE-Step engine");
    }

    CloseHandle(process.hThread);
    g_engineProcess = process.hProcess;

    if (!AssignProcessToJobObject(g_engineJob, g_engineProcess)) {
        TerminateProcess(g_engineProcess, 1);
        close_managed_handles();
        throw std::runtime_error("EtherBeat could not attach its AI engine to the app lifecycle");
    }

    constexpr int maxChecks = 240; // 120 seconds at 500 ms.
    for (int attempt = 0; attempt < maxChecks; ++attempt) {
        if (quick_health_check()) return;

        DWORD exitCode = STILL_ACTIVE;
        if (!GetExitCodeProcess(g_engineProcess, &exitCode) || exitCode != STILL_ACTIVE) {
            close_managed_handles();
            throw std::runtime_error("The private ACE-Step engine exited during startup");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    close_managed_handles();
    throw std::runtime_error("EtherBeat's private AI engine did not become ready in time");
}

} // namespace

void ensure_managed_ace_step_engine() {
    std::scoped_lock lock(g_engineMutex);
    if (quick_health_check()) return;
    provision_runtime_if_needed();
    start_managed_server();
}

bool managed_ace_step_runtime_installed() noexcept {
    try {
        RuntimePaths runtime;
        return find_runtime(runtime);
    } catch (...) {
        return false;
    }
}

bool managed_ace_step_engine_ready() noexcept {
    return quick_health_check();
}

std::filesystem::path managed_ace_step_runtime_root() {
    return preferred_runtime_root();
}

void shutdown_managed_ace_step_engine() noexcept {
    try {
        std::scoped_lock lock(g_engineMutex);
        close_managed_handles();
    } catch (...) {
    }
}

} // namespace etherbeat
