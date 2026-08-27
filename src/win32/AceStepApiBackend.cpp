#include "etherbeat/AceStepApiBackend.hpp"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace etherbeat {
namespace {

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

struct HttpResponse {
    DWORD status{};
    std::vector<std::uint8_t> body;
};

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) throw std::runtime_error("UTF-8 conversion failed");
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), size);
    return output;
}

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                out << "\\u00" << hex[(c >> 4) & 0x0f] << hex[c & 0x0f];
            } else {
                out << static_cast<char>(c);
            }
            break;
        }
    }
    return out.str();
}

HttpResponse request_http(
    const std::wstring& host,
    std::uint16_t port,
    const wchar_t* method,
    const std::wstring& path,
    const std::string& body = {},
    DWORD receive_timeout_ms = 900000) {

    InternetHandle session{WinHttpOpen(
        L"ETHERBEAT/0.1B",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0)};
    if (!session) throw std::runtime_error("WinHTTP session could not start");

    WinHttpSetTimeouts(session.get(), 5000, 5000, 30000, static_cast<int>(receive_timeout_ms));

    InternetHandle connection{WinHttpConnect(session.get(), host.c_str(), port, 0)};
    if (!connection) throw std::runtime_error("Could not connect to local ACE-Step service");

    InternetHandle request{WinHttpOpenRequest(
        connection.get(), method, path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)};
    if (!request) throw std::runtime_error("Could not create ACE-Step HTTP request");

    const wchar_t* headers = body.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : L"Content-Type: application/json\r\n";
    const DWORD header_length = body.empty() ? 0 : static_cast<DWORD>(-1L);
    LPVOID body_data = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
    const DWORD body_size = static_cast<DWORD>(body.size());

    if (!WinHttpSendRequest(
            request.get(), headers, header_length,
            body_data, body_size, body_size, 0)) {
        throw std::runtime_error("ACE-Step request could not be sent");
    }

    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        throw std::runtime_error("ACE-Step did not return a response");
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(
        request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);

    std::vector<std::uint8_t> response_body;
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            throw std::runtime_error("Could not read ACE-Step response size");
        }
        if (available == 0) break;

        const std::size_t old_size = response_body.size();
        response_body.resize(old_size + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), response_body.data() + old_size, available, &read)) {
            throw std::runtime_error("Could not read ACE-Step response body");
        }
        response_body.resize(old_size + read);
    }

    return HttpResponse{status, std::move(response_body)};
}

std::string as_text(const HttpResponse& response) {
    return std::string(response.body.begin(), response.body.end());
}

std::string extract_json_string(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = text.find(needle);
    if (pos == std::string::npos) return {};
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    pos = text.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    ++pos;

    std::string value;
    bool escape = false;
    for (; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (escape) {
            switch (c) {
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            default: value.push_back(c); break;
            }
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            break;
        } else {
            value.push_back(c);
        }
    }
    return value;
}

int extract_status(const std::string& text) {
    std::size_t pos = text.find("\"status\"");
    if (pos == std::string::npos) return -1;
    pos = text.find(':', pos);
    if (pos == std::string::npos) return -1;
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) return -1;
    return text[pos] - '0';
}

std::string extract_audio_url(const std::string& text) {
    const std::string marker = "/v1/audio?path=";
    const std::size_t begin = text.find(marker);
    if (begin == std::string::npos) return {};

    std::size_t end = begin;
    while (end < text.size()) {
        if (text[end] == '\\' && end + 1 < text.size() && text[end + 1] == '"') break;
        if (text[end] == '"') break;
        ++end;
    }
    return text.substr(begin, end - begin);
}

std::uint64_t extract_result_seed(const std::string& text, std::uint64_t fallback) {
    const std::string marker = "\\\"seed\\\":";
    std::size_t pos = text.find(marker);
    if (pos == std::string::npos) return fallback;
    pos += marker.size();
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    std::uint64_t value = 0;
    bool found = false;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        found = true;
        value = value * 10u + static_cast<unsigned>(text[pos] - '0');
        ++pos;
    }
    return found ? value : fallback;
}

std::uint64_t fallback_seed(std::uint64_t requested) {
    if (requested != 0) return requested;
    return static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
}

std::string make_generation_body(const GenerationRequest& request) {
    std::ostringstream body;
    body << "{"
         << "\"prompt\":\"" << json_escape(request.prompt) << "\","
         << "\"lyrics\":\"[Instrumental]\","
         << "\"thinking\":false,"
         << "\"instrumental\":true,"
         << "\"batch_size\":1,"
         << "\"audio_format\":\"wav\","
         << "\"audio_duration\":" << std::clamp(request.duration_seconds, 10.0, 600.0) << ","
         << "\"use_random_seed\":" << (request.seed == 0 ? "true" : "false") << ","
         << "\"seed\":" << (request.seed == 0 ? -1LL : static_cast<long long>(request.seed));

    if (request.bpm >= 30.0 && request.bpm <= 300.0) {
        body << ",\"bpm\":" << static_cast<int>(request.bpm + 0.5);
    }
    if (!request.key.empty()) {
        body << ",\"key_scale\":\"" << json_escape(request.key) << "\"";
    }
    body << "}";
    return body.str();
}

void write_metadata(
    const std::filesystem::path& path,
    const GenerationRequest& request,
    std::uint64_t seed) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Could not create ACE-Step lineage metadata");
    out << "{\n"
        << "  \"schema\": \"etherbeat.generation.v1\",\n"
        << "  \"backend\": \"ace-step-1.5-local-api\",\n"
        << "  \"seed\": " << seed << ",\n"
        << "  \"duration_seconds\": " << request.duration_seconds << ",\n"
        << "  \"bpm\": " << request.bpm << ",\n"
        << "  \"key\": \"" << json_escape(request.key) << "\",\n"
        << "  \"prompt\": \"" << json_escape(request.prompt) << "\"\n"
        << "}\n";
}

} // namespace

AceStepApiBackend::AceStepApiBackend(std::wstring host, std::uint16_t port)
    : host_(std::move(host)), port_(port) {}

std::string_view AceStepApiBackend::name() const noexcept {
    return "ace-step-1.5-local-api";
}

bool AceStepApiBackend::server_ready() const noexcept {
    try {
        const auto response = request_http(host_, port_, L"GET", L"/health", {}, 3000);
        return response.status >= 200 && response.status < 300;
    } catch (...) {
        return false;
    }
}

GenerationArtifact AceStepApiBackend::generate(
    const GenerationRequest& request,
    const std::filesystem::path& output_directory) {

    if (request.prompt.empty()) throw std::runtime_error("ACE-Step requires a prompt");
    if (!server_ready()) {
        throw std::runtime_error(
            "ACE-Step local model service is offline. Run SETUP_ACE_STEP.bat once, then START_ACE_STEP.bat, and keep that window open while EtherBeat generates.");
    }

    const auto submit = request_http(
        host_, port_, L"POST", L"/release_task", make_generation_body(request), 30000);
    const std::string submit_text = as_text(submit);
    if (submit.status < 200 || submit.status >= 300) {
        throw std::runtime_error("ACE-Step rejected the generation request: " + submit_text);
    }

    const std::string task_id = extract_json_string(submit_text, "task_id");
    if (task_id.empty()) throw std::runtime_error("ACE-Step did not return a task id");

    std::string completed_response;
    constexpr int max_polls = 450; // 15 minutes at 2 seconds per poll.
    for (int attempt = 0; attempt < max_polls; ++attempt) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        const std::string query_body = "{\"task_id_list\":[\"" + json_escape(task_id) + "\"]}";
        const auto query = request_http(host_, port_, L"POST", L"/query_result", query_body, 30000);
        const std::string query_text = as_text(query);
        if (query.status < 200 || query.status >= 300) {
            throw std::runtime_error("ACE-Step task query failed: " + query_text);
        }

        const int status = extract_status(query_text);
        if (status == 2) throw std::runtime_error("ACE-Step generation failed: " + query_text);
        if (status == 1) {
            completed_response = query_text;
            break;
        }
    }

    if (completed_response.empty()) {
        throw std::runtime_error("ACE-Step generation timed out after 15 minutes");
    }

    const std::string audio_url = extract_audio_url(completed_response);
    if (audio_url.empty()) throw std::runtime_error("ACE-Step completed but returned no downloadable audio URL");

    const auto audio = request_http(host_, port_, L"GET", utf8_to_wide(audio_url), {}, 120000);
    if (audio.status < 200 || audio.status >= 300 || audio.body.empty()) {
        throw std::runtime_error("ACE-Step audio download failed");
    }

    std::filesystem::create_directories(output_directory);
    const std::uint64_t seed = extract_result_seed(completed_response, fallback_seed(request.seed));
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::ostringstream stem;
    stem << "etherbeat_ace_" << stamp;
    const auto audio_path = output_directory / (stem.str() + ".wav");
    const auto metadata_path = output_directory / (stem.str() + ".etherbeat.json");

    std::ofstream wav(audio_path, std::ios::binary);
    if (!wav) throw std::runtime_error("Could not create local ACE-Step WAV output");
    wav.write(reinterpret_cast<const char*>(audio.body.data()), static_cast<std::streamsize>(audio.body.size()));
    if (!wav) throw std::runtime_error("Could not finish writing ACE-Step WAV output");

    write_metadata(metadata_path, request, seed);

    return GenerationArtifact{
        .audio_path = audio_path,
        .metadata_path = metadata_path,
        .backend_name = std::string{name()},
        .resolved_seed = seed
    };
}

std::unique_ptr<IModelBackend> make_ace_step_backend() {
    return std::make_unique<AceStepApiBackend>();
}

bool ace_step_server_ready() noexcept {
    return AceStepApiBackend{}.server_ready();
}

} // namespace etherbeat
