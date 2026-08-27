#include "etherbeat/AceStepEngineManager.hpp"
#include "etherbeat/AceStepApiBackend.hpp"
#include "etherbeat/GenerationTypes.hpp"

#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
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
constexpr char kPrimaryModel[] = "acestep-v15-turbo";

std::mutex g_engineMutex;
std::mutex g_statusMutex;
HANDLE g_engineProcess = nullptr;
HANDLE g_engineJob = nullptr;
EngineProvisionStatus g_status{};

class InternetHandle {
public:
    explicit InternetHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~InternetHandle() { if (handle_) WinHttpCloseHandle(handle_); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    InternetHandle(InternetHandle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    InternetHandle& operator=(InternetHandle&& other) noexcept {
        if (this != &other) {
            if (handle_) WinHttpCloseHandle(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    [[nodiscard]] HINTERNET get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
private:
    HINTERNET handle_{};
};

struct RuntimePaths {
    fs::path root;
    fs::path python;
    fs::path api_script;
};

struct HttpResponse {
    DWORD status{0};
    std::string body;
};

struct HealthInfo {
    bool api_online{false};
    bool models_initialized{false};
    std::string loaded_model;
};

void publish_state(EngineProvisionState state, std::string detail = {}, double progress = -1.0) {
    std::scoped_lock lock(g_statusMutex);
    g_status.state = state;
    g_status.detail = std::move(detail);
    if (progress >= 0.0) g_status.progress = std::clamp(progress, 0.0, 1.0);
    if (state != EngineProvisionState::Failed) g_status.error.clear();
}

void publish_failure(const std::string& error) {
    std::scoped_lock lock(g_statusMutex);
    g_status.state = EngineProvisionState::Failed;
    g_status.detail = "Engine provisioning failed";
    g_status.error = error;
    g_status.progress = 0.0;
    g_status.warmup_passed = false;
}

void set_runtime_flag(bool value) {
    std::scoped_lock lock(g_statusMutex);
    g_status.runtime_installed = value;
}

void set_api_flag(bool value) {
    std::scoped_lock lock(g_statusMutex);
    g_status.api_online = value;
}

void set_model_flags(bool files_present, bool loaded) {
    std::scoped_lock lock(g_statusMutex);
    g_status.model_files_present = files_present;
    g_status.model_loaded = loaded;
}

void set_warmup_flag(bool value) {
    std::scoped_lock lock(g_statusMutex);
    g_status.warmup_passed = value;
}

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

fs::path log_path() {
    return preferred_runtime_root() / L"logs" / L"ace-server.log";
}

bool path_is_runtime(const fs::path& root, RuntimePaths& out) {
    const fs::path api = root / L"acestep" / L"api_server.py";
    if (!fs::exists(api)) return false;

    // Official launchers use python_embedded. Some distributed portable archives
    // historically shipped the misspelled python_embeded directory, so support both.
    for (const auto& relative : {
             fs::path(L"python_embedded") / L"python.exe",
             fs::path(L"python_embeded") / L"python.exe",
             fs::path(L".venv") / L"Scripts" / L"python.exe"}) {
        const fs::path python = root / relative;
        if (fs::exists(python)) {
            out = RuntimePaths{root, python, api};
            return true;
        }
    }
    return false;
}

bool find_runtime(RuntimePaths& out) {
    std::vector<fs::path> candidates;
    try {
        const auto managed = preferred_runtime_root();
        candidates.push_back(managed / L"ACE-Step-1.5");
        candidates.push_back(managed);
    } catch (...) {}

    try {
        const auto legacy = executable_directory() / L"runtime";
        candidates.push_back(legacy / L"ACE-Step-1.5");
        candidates.push_back(legacy);
    } catch (...) {}

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!fs::exists(candidate, ec) || ec) continue;
        if (path_is_runtime(candidate, out)) return true;
    }

    try {
        const auto managed = preferred_runtime_root();
        std::error_code ec;
        if (!fs::exists(managed, ec) || ec) return false;
        for (fs::recursive_directory_iterator it(
                 managed, fs::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file(ec) || ec) continue;
            if (it->path().filename() != L"api_server.py") continue;
            if (it->path().parent_path().filename() != L"acestep") continue;
            const fs::path root = it->path().parent_path().parent_path();
            if (path_is_runtime(root, out)) return true;
        }
    } catch (...) {}
    return false;
}

bool directory_has_payload(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_directory(path, ec) || ec) return false;
    std::size_t seen = 0;
    for (fs::recursive_directory_iterator it(
             path, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (it->is_regular_file(ec) && !ec) {
            ++seen;
            if (seen >= 2) return true;
        }
    }
    return false;
}

bool primary_model_files_present(const RuntimePaths& runtime) {
    return directory_has_payload(runtime.root / L"checkpoints" / L"acestep-v15-turbo");
}

HttpResponse local_request(
    const wchar_t* method,
    const wchar_t* path,
    const std::string& body = {},
    DWORD receive_timeout_ms = 5000) {

    InternetHandle session{WinHttpOpen(
        L"ETHERBEAT/0.3-PROVISIONER",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0)};
    if (!session) throw std::runtime_error("Could not open local engine HTTP session");
    WinHttpSetTimeouts(session.get(), 3000, 3000, 10000, static_cast<int>(receive_timeout_ms));

    InternetHandle connection{WinHttpConnect(session.get(), kHost, kPort, 0)};
    if (!connection) throw std::runtime_error("Could not connect to ACE-Step localhost API");

    InternetHandle request{WinHttpOpenRequest(
        connection.get(), method, path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)};
    if (!request) throw std::runtime_error("Could not create ACE-Step localhost request");

    const wchar_t* headers = body.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : L"Content-Type: application/json\r\n";
    const DWORD header_length = body.empty() ? 0 : static_cast<DWORD>(-1L);
    LPVOID body_data = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
    const DWORD body_size = static_cast<DWORD>(body.size());
    if (!WinHttpSendRequest(request.get(), headers, header_length, body_data, body_size, body_size, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        throw std::runtime_error("ACE-Step localhost request failed");
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(
            request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        throw std::runtime_error("ACE-Step localhost response had no HTTP status");
    }

    std::string response;
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            throw std::runtime_error("Could not read ACE-Step localhost response");
        }
        if (available == 0) break;
        const auto old_size = response.size();
        response.resize(old_size + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), response.data() + old_size, available, &read)) {
            throw std::runtime_error("Could not read ACE-Step localhost response body");
        }
        response.resize(old_size + read);
    }
    return {status, std::move(response)};
}

bool json_bool(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = text.find(needle);
    if (pos == std::string::npos) return false;
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    return text.compare(pos, 4, "true") == 0;
}

std::string json_string(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = text.find(needle);
    if (pos == std::string::npos) return {};
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    pos = text.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    ++pos;
    std::string value;
    bool escaped = false;
    for (; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (escaped) {
            value.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            break;
        } else {
            value.push_back(c);
        }
    }
    return value;
}

HealthInfo query_health() noexcept {
    try {
        const auto response = local_request(L"GET", L"/health", {}, 3000);
        if (response.status < 200 || response.status >= 300) return {};
        HealthInfo info;
        info.api_online = true;
        info.models_initialized = json_bool(response.body, "models_initialized");
        info.loaded_model = json_string(response.body, "loaded_model");
        return info;
    } catch (...) {
        return {};
    }
}

void download_portable_runtime(const fs::path& archive) {
    publish_state(EngineProvisionState::RuntimeDownloading,
                  "Downloading ACE-Step portable runtime", 0.0);

    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(kPortableRuntimeUrl, 0, 0, &components)) {
        throw std::runtime_error("Could not parse official ACE-Step runtime URL");
    }

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength) path.append(components.lpszExtraInfo, components.dwExtraInfoLength);

    InternetHandle session{WinHttpOpen(
        L"ETHERBEAT/0.3-PROVISIONER",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0)};
    if (!session) throw std::runtime_error("Could not open runtime downloader");
    WinHttpSetTimeouts(session.get(), 10000, 10000, 30000, 300000);

    InternetHandle connection{WinHttpConnect(session.get(), host.c_str(), components.nPort, 0)};
    if (!connection) throw std::runtime_error("Could not reach ACE-Step runtime host");
    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle request{WinHttpOpenRequest(
        connection.get(), L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
    if (!request) throw std::runtime_error("Could not create runtime download request");
    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        throw std::runtime_error("Could not download ACE-Step portable runtime");
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) {
        throw std::runtime_error("ACE-Step runtime download returned HTTP " + std::to_string(status));
    }

    unsigned long long expected = 0;
    wchar_t content_length[64]{};
    DWORD content_length_size = sizeof(content_length);
    if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_CONTENT_LENGTH,
                            WINHTTP_HEADER_NAME_BY_INDEX, content_length, &content_length_size,
                            WINHTTP_NO_HEADER_INDEX)) {
        expected = _wcstoui64(content_length, nullptr, 10);
    }

    fs::create_directories(archive.parent_path());
    const fs::path partial = archive.wstring() + L".part";
    std::ofstream output(partial, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Could not create runtime archive on disk");

    unsigned long long downloaded = 0;
    std::vector<std::uint8_t> buffer(1024 * 1024);
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            throw std::runtime_error("Runtime download was interrupted");
        }
        if (!available) break;
        while (available > 0) {
            const DWORD chunk = static_cast<DWORD>((std::min)(static_cast<std::size_t>(available), buffer.size()));
            DWORD read = 0;
            if (!WinHttpReadData(request.get(), buffer.data(), chunk, &read)) {
                throw std::runtime_error("Runtime download failed while reading data");
            }
            if (!read) break;
            output.write(reinterpret_cast<const char*>(buffer.data()), read);
            if (!output) throw std::runtime_error("Runtime download failed while writing data");
            downloaded += read;
            available -= read;
            const double progress = expected ? static_cast<double>(downloaded) / static_cast<double>(expected) : 0.0;
            const auto mb = downloaded / (1024ull * 1024ull);
            publish_state(EngineProvisionState::RuntimeDownloading,
                          "Downloading runtime // " + std::to_string(mb) + " MB", progress);
        }
    }
    output.close();
    if (downloaded < 1024ull * 1024ull) {
        throw std::runtime_error("Downloaded ACE-Step archive is unexpectedly small");
    }

    std::error_code ec;
    fs::remove(archive, ec);
    fs::rename(partial, archive, ec);
    if (ec) throw std::runtime_error("Could not finalize downloaded ACE-Step archive");
}

DWORD run_hidden_and_wait(std::wstring command, const fs::path& working_directory) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, working_directory.empty() ? nullptr : working_directory.c_str(),
                        &startup, &process)) {
        throw std::runtime_error("Could not start Windows runtime extractor");
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    return exit_code;
}

void provision_runtime_if_needed() {
    RuntimePaths runtime;
    if (find_runtime(runtime)) {
        set_runtime_flag(true);
        publish_state(EngineProvisionState::RuntimeReady, "ACE-Step runtime verified", 1.0);
        return;
    }

    set_runtime_flag(false);
    publish_state(EngineProvisionState::RuntimeMissing, "ACE-Step runtime is not installed", 0.0);
    const fs::path root = preferred_runtime_root();
    fs::create_directories(root);
    const fs::path archive = root / L"ACE-Step-1.5.7z";
    if (!fs::exists(archive)) download_portable_runtime(archive);

    publish_state(EngineProvisionState::RuntimeExtracting,
                  "Extracting ACE-Step portable runtime", 0.0);
    std::wstring command = L"tar.exe -xf \"" + archive.wstring() + L"\" -C \"" + root.wstring() + L"\"";
    const DWORD extraction_exit = run_hidden_and_wait(command, root);
    if (extraction_exit != 0) {
        throw std::runtime_error(
            "ACE-Step downloaded, but Windows tar.exe could not extract the .7z archive (exit " +
            std::to_string(extraction_exit) + ")");
    }
    if (!find_runtime(runtime)) {
        throw std::runtime_error(
            "ACE-Step extracted, but EtherBeat could not locate api_server.py plus embedded Python. "
            "Supported folders: python_embedded, python_embeded, or .venv/Scripts.");
    }

    std::error_code ec;
    fs::remove(archive, ec);
    set_runtime_flag(true);
    publish_state(EngineProvisionState::RuntimeReady, "ACE-Step runtime installed", 1.0);
}

void close_managed_handles() noexcept {
    if (g_engineJob) {
        CloseHandle(g_engineJob);
        g_engineJob = nullptr;
    }
    if (g_engineProcess) {
        CloseHandle(g_engineProcess);
        g_engineProcess = nullptr;
    }
}

void start_managed_server() {
    const auto existing = query_health();
    if (existing.api_online) {
        set_api_flag(true);
        set_model_flags(false, existing.models_initialized);
        publish_state(existing.models_initialized ? EngineProvisionState::ModelLoading : EngineProvisionState::ApiOnline,
                      "ACE-Step localhost API already running");
        return;
    }

    RuntimePaths runtime;
    if (!find_runtime(runtime)) throw std::runtime_error("ACE-Step runtime is not installed correctly");

    if (g_engineProcess) {
        DWORD exit_code = STILL_ACTIVE;
        if (!GetExitCodeProcess(g_engineProcess, &exit_code) || exit_code != STILL_ACTIVE) close_managed_handles();
    }

    g_engineJob = CreateJobObjectW(nullptr, nullptr);
    if (!g_engineJob) throw std::runtime_error("Could not create ACE-Step process container");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(g_engineJob, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        close_managed_handles();
        throw std::runtime_error("Could not secure ACE-Step process container");
    }

    // Bring the API up first, then explicitly initialize/download the model through /v1/init.
    // This is what lets EtherBeat distinguish API_ONLINE from MODEL_LOADED.
    SetEnvironmentVariableW(L"ACESTEP_NO_INIT", L"true");
    SetEnvironmentVariableW(L"ACESTEP_INIT_LLM", L"false");
    SetEnvironmentVariableW(L"ACESTEP_CONFIG_PATH", L"acestep-v15-turbo");

    const auto logs = log_path();
    fs::create_directories(logs.parent_path());
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE log_file = CreateFileW(logs.c_str(), FILE_APPEND_DATA,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log_file == INVALID_HANDLE_VALUE) {
        close_managed_handles();
        throw std::runtime_error("Could not create ACE-Step server log file");
    }
    SetFilePointer(log_file, 0, nullptr, FILE_END);

    std::wstring command = L"\"" + runtime.python.wstring() + L"\" \"" +
                           runtime.api_script.wstring() + L"\" --host 127.0.0.1 --port 8001";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = log_file;
    startup.hStdError = log_file;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process{};
    publish_state(EngineProvisionState::ApiStarting,
                  "Starting ACE-Step localhost API // log: " + logs.string(), 0.0);
    if (!CreateProcessW(runtime.python.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr,
                        runtime.root.c_str(), &startup, &process)) {
        CloseHandle(log_file);
        close_managed_handles();
        throw std::runtime_error("Could not launch ACE-Step embedded Python API server");
    }
    CloseHandle(log_file);
    CloseHandle(process.hThread);
    g_engineProcess = process.hProcess;
    if (!AssignProcessToJobObject(g_engineJob, g_engineProcess)) {
        TerminateProcess(g_engineProcess, 1);
        close_managed_handles();
        throw std::runtime_error("Could not attach ACE-Step process to EtherBeat lifecycle");
    }

    constexpr int max_checks = 360; // 180 seconds.
    for (int attempt = 0; attempt < max_checks; ++attempt) {
        const auto health = query_health();
        if (health.api_online) {
            set_api_flag(true);
            set_model_flags(primary_model_files_present(runtime), health.models_initialized);
            publish_state(EngineProvisionState::ApiOnline,
                          "ACE-Step API online // model not yet trusted", 1.0);
            return;
        }
        DWORD exit_code = STILL_ACTIVE;
        if (!GetExitCodeProcess(g_engineProcess, &exit_code) || exit_code != STILL_ACTIVE) {
            close_managed_handles();
            throw std::runtime_error(
                "ACE-Step API process exited during startup. Open ENGINE > OPEN RUNTIME and inspect logs/ace-server.log");
        }
        if (attempt % 10 == 0) {
            publish_state(EngineProvisionState::ApiStarting,
                          "Waiting for localhost API // " + std::to_string(attempt / 2) + " sec",
                          static_cast<double>(attempt) / max_checks);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    close_managed_handles();
    throw std::runtime_error("ACE-Step API did not become reachable within 180 seconds; inspect logs/ace-server.log");
}

bool initialize_primary_model() {
    RuntimePaths runtime;
    if (!find_runtime(runtime)) throw std::runtime_error("ACE-Step runtime disappeared before model initialization");

    auto health = query_health();
    if (!health.api_online) throw std::runtime_error("ACE-Step API went offline before model initialization");
    const bool files_present = primary_model_files_present(runtime);
    set_api_flag(true);
    set_model_flags(files_present, health.models_initialized);
    if (health.models_initialized) return true;

    publish_state(files_present ? EngineProvisionState::ModelLoading : EngineProvisionState::ModelMissing,
                  files_present ? "Model files found; preparing DiT" : "Primary DiT model is not downloaded", 0.0);
    publish_state(files_present ? EngineProvisionState::ModelLoading : EngineProvisionState::ModelDownloading,
                  files_present ? "Loading acestep-v15-turbo into memory" : "Downloading + loading acestep-v15-turbo (first run can be large)",
                  0.05);

    const auto response = local_request(
        L"POST", L"/v1/init",
        "{\"model\":\"acestep-v15-turbo\",\"init_llm\":false}",
        45u * 60u * 1000u);

    // Older ACE builds may not expose the explicit init endpoint. In that case
    // the warmup request below is allowed to trigger ACE's documented lazy init.
    if (response.status == 404 || response.status == 405) {
        publish_state(EngineProvisionState::ModelLoading,
                      "ACE build uses lazy initialization; warmup will load the model", 0.5);
        return false;
    }
    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error("ACE-Step /v1/init failed with HTTP " + std::to_string(response.status) + ": " + response.body);
    }
    if (response.body.find("\"code\":500") != std::string::npos ||
        response.body.find("\"code\": 500") != std::string::npos) {
        const auto error = json_string(response.body, "error");
        throw std::runtime_error("ACE-Step model initialization failed: " + (error.empty() ? response.body : error));
    }

    for (int attempt = 0; attempt < 120; ++attempt) {
        health = query_health();
        if (health.api_online && health.models_initialized) {
            set_model_flags(true, true);
            publish_state(EngineProvisionState::ModelLoading,
                          "acestep-v15-turbo loaded; preparing real inference self-test", 1.0);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return false;
}

void run_real_warmup() {
    publish_state(EngineProvisionState::Warmup,
                  "Running real 10-second generation self-test", 0.0);
    set_warmup_flag(false);

    GenerationRequest request;
    request.mode = GenerationMode::TextToInstrumental;
    request.render_intent = RenderIntent::Draft;
    request.prompt = "EtherBeat engine self-test: sparse neutral instrumental pulse, simple drums, no vocals";
    request.duration_seconds = 10.0;
    request.bpm = 72.0;
    request.key = "A minor";
    request.seed = 424242;

    const fs::path warmup_root = preferred_runtime_root() / L"warmup";
    std::error_code ec;
    fs::create_directories(warmup_root, ec);
    AceStepApiBackend backend;
    const auto artifact = backend.generate(request, warmup_root);
    if (artifact.audio_path.empty() || !fs::exists(artifact.audio_path, ec) || ec) {
        throw std::runtime_error("ACE-Step warmup returned no audio file");
    }
    const auto size = fs::file_size(artifact.audio_path, ec);
    if (ec || size < 44) throw std::runtime_error("ACE-Step warmup audio file is invalid or empty");

    const auto health = query_health();
    if (!health.api_online || !health.models_initialized) {
        throw std::runtime_error("Warmup generated audio but ACE /health still reports models_initialized=false");
    }

    set_api_flag(true);
    set_model_flags(true, true);
    set_warmup_flag(true);
    publish_state(EngineProvisionState::Ready,
                  "REAL MODEL READY // warmup WAV verified // generation unlocked", 1.0);
}

} // namespace

void ensure_managed_ace_step_engine() {
    std::scoped_lock lock(g_engineMutex);
    try {
        {
            std::scoped_lock status_lock(g_statusMutex);
            if (g_status.ready()) return;
            g_status.warmup_passed = false;
        }

        provision_runtime_if_needed();
        RuntimePaths runtime;
        if (!find_runtime(runtime)) throw std::runtime_error("Runtime verification failed after provisioning");
        set_runtime_flag(true);
        set_model_flags(primary_model_files_present(runtime), false);

        start_managed_server();
        static_cast<void>(initialize_primary_model());
        run_real_warmup();
    } catch (const std::exception& e) {
        publish_failure(e.what());
        throw;
    }
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
    std::scoped_lock lock(g_statusMutex);
    return g_status.ready();
}

EngineProvisionStatus managed_ace_step_engine_status() noexcept {
    try {
        EngineProvisionStatus snapshot;
        {
            std::scoped_lock lock(g_statusMutex);
            snapshot = g_status;
        }
        const bool installed = managed_ace_step_runtime_installed();
        snapshot.runtime_installed = installed;
        if (snapshot.state == EngineProvisionState::RuntimeMissing && installed) {
            snapshot.state = EngineProvisionState::RuntimeReady;
            snapshot.detail = "Runtime found; press START / VERIFY to load and self-test model";
        }
        return snapshot;
    } catch (...) {
        return {};
    }
}

std::filesystem::path managed_ace_step_runtime_root() {
    return preferred_runtime_root();
}

std::filesystem::path managed_ace_step_log_path() {
    return log_path();
}

void shutdown_managed_ace_step_engine() noexcept {
    try {
        std::scoped_lock lock(g_engineMutex);
        close_managed_handles();
        const bool installed = managed_ace_step_runtime_installed();
        {
            std::scoped_lock status_lock(g_statusMutex);
            g_status = {};
            g_status.runtime_installed = installed;
            g_status.state = installed ? EngineProvisionState::RuntimeReady : EngineProvisionState::RuntimeMissing;
            g_status.detail = installed ? "Runtime installed; model server stopped" : "Runtime not installed";
        }
    } catch (...) {}
}

} // namespace etherbeat
