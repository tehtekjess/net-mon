#include "ipc/pipe.hpp"
#include <sddl.h>
#include <shlobj.h>
#include <stdexcept>
namespace nm {
namespace {
constexpr DWORD max_message = 2 * 1024 * 1024;
void transfer(HANDLE pipe, void *data, DWORD size, bool write, HANDLE stop = nullptr) {
    auto *cursor = static_cast<unsigned char *>(data);
    DWORD done = 0;
    while (done < size) {
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        OVERLAPPED ov{};
        ov.hEvent = event.get();
        DWORD count = 0;
        BOOL result = write ? WriteFile(pipe, cursor + done, size - done, &count, &ov)
                            : ReadFile(pipe, cursor + done, size - done, &count, &ov);
        if (!result) {
            if (GetLastError() != ERROR_IO_PENDING)
                throw std::runtime_error(win_error());
            HANDLE handles[] = {event.get(), stop};
            DWORD wait = WaitForMultipleObjects(stop ? 2 : 1, handles, FALSE, 3000);
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &ov);
                GetOverlappedResult(pipe, &ov, &count, TRUE);
                throw std::runtime_error("IPC timeout or cancellation");
            }
            if (!GetOverlappedResult(pipe, &ov, &count, FALSE))
                throw std::runtime_error(win_error());
        }
        if (!count)
            throw std::runtime_error("IPC connection closed");
        done += count;
    }
}
Json receive(HANDLE pipe, HANDLE stop = nullptr) {
    DWORD size = 0;
    transfer(pipe, &size, sizeof(size), false, stop);
    if (size == 0 || size > max_message)
        throw std::runtime_error("Invalid IPC message size");
    std::string data(size, '\0');
    transfer(pipe, data.data(), size, false, stop);
    // Cap parser nesting before allocating recursive JSON values.
    int depth = 0;
    return Json::parse(data, [&depth](int, Json::parse_event_t event, Json &) {
        if (event == Json::parse_event_t::object_start ||
            event == Json::parse_event_t::array_start) {
            if (++depth > 32)
                throw std::runtime_error("JSON nesting limit exceeded");
        } else if (event == Json::parse_event_t::object_end ||
                   event == Json::parse_event_t::array_end)
            --depth;
        return true;
    });
}
void send(HANDLE pipe, const Json &data, HANDLE stop = nullptr) {
    auto text = data.dump();
    if (text.size() > max_message)
        throw std::runtime_error("IPC response exceeds limit; request fewer rows");
    DWORD size = static_cast<DWORD>(text.size());
    transfer(pipe, &size, sizeof(size), true, stop);
    transfer(pipe, text.data(), size, true, stop);
}
bool authorized(HANDLE pipe, const std::string &owner) {
    if (!ImpersonateNamedPipeClient(pipe))
        return false;
    bool result = false;
    try {
        HANDLE raw = nullptr;
        if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw)) {
            Handle token(raw);
            auto sid = token_sid(token.get());
            result = sid == owner || sid == "S-1-5-18" || is_admin();
        }
    } catch (...) {
    }
    RevertToSelf();
    return result;
}
void verify_server(HANDLE pipe, const std::wstring &name) {
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &pid))
        throw std::runtime_error("Cannot identify service");
    if (name == service_pipe) {
        // A standard user cannot always open a LocalSystem process. SCM is the
        // trusted authority for the installed service's PID and protected image path.
        struct ServiceHandle {
            SC_HANDLE value;
            ~ServiceHandle() {
                if (value)
                    CloseServiceHandle(value);
            }
        };
        ServiceHandle manager{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
        if (!manager.value)
            throw std::runtime_error("Cannot inspect service manager");
        ServiceHandle service{
            OpenServiceW(manager.value, L"NetMon", SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG)};
        if (!service.value)
            throw std::runtime_error("Cannot inspect installed NetMon service");
        SERVICE_STATUS_PROCESS status{};
        DWORD needed = 0;
        if (!QueryServiceStatusEx(service.value, SC_STATUS_PROCESS_INFO,
                                  reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed) ||
            status.dwProcessId != pid || status.dwCurrentState != SERVICE_RUNNING)
            throw std::runtime_error("Pipe owner does not match the running NetMon service");
        QueryServiceConfigW(service.value, nullptr, 0, &needed);
        if (!needed || needed > 65536)
            throw std::runtime_error("Invalid service configuration");
        std::vector<unsigned char> buffer(needed);
        auto *configuration = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
        if (!QueryServiceConfigW(service.value, configuration, needed, &needed))
            throw std::runtime_error(win_error());
        PWSTR program_files = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &program_files)))
            throw std::runtime_error("Program Files unavailable");
        auto expected = std::filesystem::path(program_files) / L"NetMon" / L"netmon_service.exe";
        CoTaskMemFree(program_files);
        const auto command = L"\"" + expected.wstring() + L"\" --service";
        if (_wcsicmp(configuration->lpBinaryPathName, command.c_str()) != 0)
            throw std::runtime_error("Unexpected installed service image");
        return;
    }
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process)
        throw std::runtime_error("Cannot verify service process");
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &size))
        throw std::runtime_error(win_error());
    path.resize(size);
    auto expected = executable_path().parent_path() / L"netmon_service.exe";
    if (_wcsicmp(path.c_str(), expected.c_str()) != 0)
        throw std::runtime_error("Unexpected process owns NetMon pipe");
}
} // namespace
std::wstring demo_pipe() { return L"\\\\.\\pipe\\NetMon.Demo." + wide(current_sid()); }
Json call_pipe(const std::wstring &name, const Json &request, HANDLE cancel) {
    if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0)
        throw std::runtime_error("Request cancelled");
    if (!WaitNamedPipeW(name.c_str(), 500))
        throw std::runtime_error(
            "Monitoring service is not running. Install/start NetMon, or open the demo.");
    Handle pipe(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
                            nullptr));
    if (!pipe)
        throw std::runtime_error(win_error());
    verify_server(pipe.get(), name);
    send(pipe.get(), request, cancel);
    auto reply = receive(pipe.get(), cancel);
    unsigned char acknowledged = 1;
    transfer(pipe.get(), &acknowledged, 1, true, cancel);
    if (!reply.value("ok", false))
        throw std::runtime_error(reply.value("error", "Service request failed"));
    return reply.at("data");
}
PipeServer::PipeServer(HANDLE stop, std::wstring name, std::string owner,
                       std::function<Json(const Json &, const std::function<bool()> &)> handler)
    : stop_(stop), name_(std::move(name)), owner_(std::move(owner)), handler_(std::move(handler)) {
    // Validate before interpolation into the ACL.
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(wide(owner_).c_str(), &sid))
        throw std::runtime_error("Invalid owner SID");
    LocalFree(sid);
    const auto sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;" + wide(owner_) + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                              &descriptor, nullptr))
        throw std::runtime_error("Pipe security: " + win_error());
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    constexpr DWORD instances = 4;
    for (DWORD i = 0; i < instances; ++i) {
        Handle pipe(CreateNamedPipeW(name_.c_str(),
                                     PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                                         (i == 0 ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                                     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                         PIPE_REJECT_REMOTE_CLIENTS,
                                     instances, 65536, 65536, 0, &attributes));
        if (!pipe) {
            const auto error = win_error();
            LocalFree(descriptor);
            throw std::runtime_error("CreateNamedPipe: " + error);
        }
        pipes_.push_back(std::move(pipe));
    }
    LocalFree(descriptor);
    try {
        for (const auto &pipe : pipes_)
            threads_.emplace_back([this, handle = pipe.get()] { run(handle); });
    } catch (...) {
        SetEvent(stop_);
        for (auto &thread : threads_)
            if (thread.joinable())
                thread.join();
        throw;
    }
}
PipeServer::~PipeServer() {
    SetEvent(stop_);
    for (auto &thread : threads_)
        if (thread.joinable())
            thread.join();
}
void PipeServer::run(HANDLE pipe) {
    while (WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) {
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        OVERLAPPED ov{};
        ov.hEvent = event.get();
        BOOL connected = ConnectNamedPipe(pipe, &ov);
        DWORD error = connected ? ERROR_SUCCESS : GetLastError();
        if (error == ERROR_IO_PENDING) {
            HANDLE handles[] = {stop_, event.get()};
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
                CancelIoEx(pipe, &ov);
                DWORD count = 0;
                GetOverlappedResult(pipe, &ov, &count, TRUE);
                break;
            }
            DWORD count = 0;
            connected = GetOverlappedResult(pipe, &ov, &count, FALSE);
        } else
            connected = error == ERROR_PIPE_CONNECTED || error == ERROR_SUCCESS;
        if (connected) {
            try {
                auto request = receive(pipe, stop_);
                if (!authorized(pipe, owner_))
                    throw std::runtime_error("Access denied");
                Json reply;
                try {
                    const auto alive = [this, pipe] {
                        return WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0 &&
                               PeekNamedPipe(pipe, nullptr, 0, nullptr, nullptr, nullptr) != FALSE;
                    };
                    reply = {{"ok", true}, {"data", handler_(request, alive)}};
                } catch (const std::exception &ex) {
                    reply = {{"ok", false}, {"error", ex.what()}};
                }
                send(pipe, reply, stop_);
                // DisconnectNamedPipe discards unread bytes. A bounded ACK ensures delivery.
                unsigned char acknowledged = 0;
                transfer(pipe, &acknowledged, 1, false, stop_);
            } catch (...) {
            }
            DisconnectNamedPipe(pipe);
        } else if (WaitForSingleObject(stop_, 100) == WAIT_OBJECT_0)
            break;
    }
}
} // namespace nm
