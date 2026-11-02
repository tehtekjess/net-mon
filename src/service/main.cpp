#include "collectors/audit.hpp"
#include "core/engine.hpp"
#include "ipc/pipe.hpp"
#include <psapi.h>
#include <iostream>
#include <memory>
#include <thread>
namespace {
nm::Handle stop_event;
SERVICE_STATUS_HANDLE service_handle = nullptr;
SERVICE_STATUS service_status{};
void status(DWORD state, DWORD code = 0) {
    service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    service_status.dwCurrentState = state;
    service_status.dwControlsAccepted =
        state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    service_status.dwWin32ExitCode = code;
    service_status.dwWaitHint = state == SERVICE_STOP_PENDING ? 10000 : 0;
    if (service_handle)
        SetServiceStatus(service_handle, &service_status);
}
DWORD WINAPI control(DWORD code, DWORD, LPVOID, LPVOID) {
    if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) {
        status(SERVICE_STOP_PENDING);
        SetEvent(stop_event.get());
    }
    return NO_ERROR;
}
BOOL WINAPI console_control(DWORD code) {
    if (code == CTRL_C_EVENT || code == CTRL_BREAK_EVENT || code == CTRL_CLOSE_EVENT) {
        SetEvent(stop_event.get());
        return TRUE;
    }
    return FALSE;
}
void run(const std::filesystem::path &root, bool demo, int seconds) {
    nm::Handle singleton(
        CreateMutexW(nullptr, FALSE,
                     (demo ? (L"Local\\NetMon.DemoService." + nm::wide(nm::current_sid()))
                           : L"Global\\NetMon.Service")
                         .c_str()));
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS)
        throw std::runtime_error("NetMon service is already running");
    stop_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!stop_event)
        throw std::runtime_error(nm::win_error());
    nm::Engine engine(root);
    auto owner =
        demo ? nm::current_sid()
             : nm::Json::parse(nm::read_text(root / L"owner.json")).at("sid").get<std::string>();
    std::unique_ptr<nm::AuditCollector> collector;
    if (!demo)
        collector = std::make_unique<nm::AuditCollector>(
            stop_event.get(), engine.checkpoint(), [&](nm::Event event, std::string bookmark) {
                engine.enqueue(std::move(event), std::move(bookmark));
            });
    nm::PipeServer server(
        stop_event.get(), demo ? nm::demo_pipe() : nm::service_pipe, owner,
        [&](const nm::Json &request, const std::function<bool()> &connected) {
            auto response = engine.request(request, connected);
            if (request.value("op", "") == "status") {
                response["demo"] = demo;
                PROCESS_MEMORY_COUNTERS_EX memory{};
                memory.cb = sizeof(memory);
                if (GetProcessMemoryInfo(GetCurrentProcess(),
                                         reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                                         sizeof(memory))) {
                    response["private_bytes"] = memory.PrivateUsage;
                    response["working_set_bytes"] = memory.WorkingSetSize;
                }
                FILETIME created{}, exited{}, kernel{}, user{};
                if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
                    const auto ticks = [](const FILETIME &value) {
                        return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) |
                               value.dwLowDateTime;
                    };
                    response["cpu_milliseconds"] = (ticks(kernel) + ticks(user)) / 10000;
                }
                if (collector) {
                    const auto h = collector->health();
                    response["collector"] = {
                        {"subscribed", h.subscribed}, {"errors", h.errors}, {"message", h.message}};
                    try {
                        auto flags = nm::audit_policy();
                        response["audit_enabled"] = (flags & 3) == 3;
                    } catch (...) {
                        response["audit_enabled"] = false;
                    }
                } else {
                    response["collector"] = {
                        {"subscribed", true},
                        {"errors", 0},
                        {"message", "Synthetic demonstration; no traffic captured"}};
                    response["audit_enabled"] = false;
                }
            }
            return response;
        });
    status(SERVICE_RUNNING);
    if (demo) {
        auto config = engine.request({{"op", "config"}}).get<nm::Config>();
        config.settings.log_all = true;
        config.settings.alert_mode = "ask";
        engine.request({{"op", "settings"}, {"settings", config.settings}});
    }
    auto start = nm::now_ms();
    int count = 0;
    while (WaitForSingleObject(stop_event.get(), demo ? 1500 : 1000) != WAIT_OBJECT_0) {
        if (seconds > 0 && nm::now_ms() - start >= static_cast<nm::Millis>(seconds) * 1000) {
            SetEvent(stop_event.get());
            break;
        }
        if (demo) {
            nm::Event e;
            e.id = nm::random_id();
            e.time = nm::now_ms();
            e.source = "Synthetic demo";
            e.pid = GetCurrentProcessId();
            e.app = nm::utf8(nm::executable_path().wstring());
            e.local_ip = "192.0.2.10";
            e.remote_ip = count % 2 ? "203.0.113.20" : "2001:db8::42";
            e.protocol = count % 3 ? 6 : 17;
            e.remote_port = count % 3 ? 443 : 53;
            e.local_port = static_cast<std::uint16_t>(50000 + count % 1000);
            ++count;
            engine.enqueue(std::move(e), "");
        }
    }
    collector.reset();
    engine.stop();
}
void WINAPI service_main(DWORD, LPWSTR *) {
    service_handle = RegisterServiceCtrlHandlerExW(L"NetMon", control, nullptr);
    if (!service_handle)
        return;
    status(SERVICE_START_PENDING);
    try {
        run(nm::data_dir(), false, 0);
        status(SERVICE_STOPPED);
    } catch (...) {
        status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
    }
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        std::wstring mode = argc > 1 ? argv[1] : L"--service";
        if (mode == L"--enable-audit") {
            nm::enable_audit(nm::data_dir());
            std::cout << "WFP success/failure auditing enabled.\n";
            return 0;
        }
        if (mode == L"--restore-audit") {
            nm::restore_audit(nm::data_dir());
            std::cout << "Owned audit policy changes restored where unchanged.\n";
            return 0;
        }
        if (mode == L"--probe") {
            nm::Json out = {{"architecture", "x64"},
                            {"administrator", nm::is_admin()},
                            {"data_directory", nm::utf8(nm::data_dir().wstring())}};
            try {
                out["audit_flags"] = nm::audit_policy();
            } catch (const std::exception &ex) {
                out["audit_error"] = ex.what();
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }
        if (mode == L"--request" || mode == L"--demo-request" || mode == L"--request-file" ||
            mode == L"--demo-request-file") {
            if (argc < 3)
                throw std::runtime_error("Expected JSON request");
            const bool demo = mode == L"--demo-request" || mode == L"--demo-request-file";
            const bool file = mode == L"--request-file" || mode == L"--demo-request-file";
            auto request =
                nm::Json::parse(file ? nm::read_text(argv[2], 1024 * 1024) : nm::utf8(argv[2]));
            std::cout << nm::call_pipe(demo ? nm::demo_pipe() : nm::service_pipe, request).dump(2)
                      << "\n";
            return 0;
        }
        if (mode == L"--console" || mode == L"--demo") {
            SetConsoleCtrlHandler(console_control, TRUE);
            std::filesystem::path root = nm::data_dir();
            int seconds = 0;
            for (int i = 2; i < argc; ++i) {
                if (std::wstring(argv[i]) == L"--data" && i + 1 < argc)
                    root = argv[++i];
                else if (std::wstring(argv[i]) == L"--seconds" && i + 1 < argc)
                    seconds = std::stoi(argv[++i]);
                else
                    throw std::runtime_error("Unknown argument");
            }
            if (mode == L"--demo" && root == nm::data_dir())
                throw std::runtime_error("Demo requires --data with an isolated directory");
            run(root, mode == L"--demo", seconds);
            return 0;
        }
        if (mode != L"--service")
            throw std::runtime_error("Unknown command");
        SERVICE_TABLE_ENTRYW table[] = {{const_cast<wchar_t *>(L"NetMon"), service_main},
                                        {nullptr, nullptr}};
        if (!StartServiceCtrlDispatcherW(table))
            throw std::runtime_error(nm::win_error());
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
