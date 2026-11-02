// Exercise the complete production window/message loop, not only TableView.
#include "ipc/pipe.hpp"
#include <shellapi.h>
#include <chrono>
#include <iostream>
#include <map>
#include <thread>
#include <atomic>
#include <algorithm>
namespace {
using Clock = std::chrono::steady_clock;
std::vector<double> dispatch_times, queue_times, action_times;
LARGE_INTEGER counter_frequency{};
WPARAM counter() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<WPARAM>(value.QuadPart);
}
std::map<UINT, double> message_max;
std::map<std::string, std::vector<double>> action_groups;
std::map<std::uint64_t, Clock::time_point> refresh_starts;
std::vector<double> refresh_times;
std::atomic<unsigned> stress_cancellations{0};
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
} // namespace
#define wWinMain netmon_unused_entry
// Include the production implementation to exercise its actual handlers and workers.
// Only the entry point and read-only IPC seam differ in this test executable.
#include "../src/ui/main.cpp"
#undef wWinMain
namespace {
constexpr UINT ping_message = WM_APP + 40, finish_message = WM_APP + 41;
constexpr UINT_PTR exercise_timer = 40;
int actions = 0, tab_checks = 0, view_changes = 0, selections = 0;
LRESULT CALLBACK measured_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    const auto start = Clock::now();
    LRESULT result = 0;
    if (m == ping_message) {
        queue_times.push_back(static_cast<double>(counter() - w) * 1000.0 /
                              static_cast<double>(counter_frequency.QuadPart));
        return 0;
    }
    if (m == finish_message) {
        DestroyWindow(h);
        return 0;
    }
    if (m == WM_TIMER && w == exercise_timer) {
        // Only this test process's own controls are exercised.
        std::string action;
        if (actions % 4 == 0) {
            action = "switch_view";
            const int next = view_changes % 3;
            SendMessageW(nav_buttons[static_cast<std::size_t>(next)], BM_CLICK, 0, 0);
            if (view_selection == next)
                ++tab_checks;
            refresh_starts[view_generation] = start;
            ++view_changes;
        } else if (view_selection == 2) {
            action = actions % 4 == 1 ? "map_zoom" : actions % 4 == 2 ? "map_country" : "theme";
            if (action == "map_zoom")
                SendMessageW(GetDlgItem(world_view.window(), 4), BM_CLICK, 0, 0);
            else if (action == "map_country")
                SendMessageW(world_view.window(), WM_KEYDOWN, VK_RIGHT, 0);
            else
                SendMessageW(GetDlgItem(window, id_theme), BM_CLICK, 0, 0);
        } else if (ListView_GetItemCount(list) > 0) {
            action = "select_row";
            ListView_SetItemState(list, selections % ListView_GetItemCount(list),
                                  LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ++selections;
        }
        ++actions;
        action_times.push_back(elapsed(start));
        action_groups[action].push_back(elapsed(start));
        const auto paint = Clock::now();
        RedrawWindow(h, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
        action_groups["paint_after_" + action].push_back(elapsed(paint));
        return 0;
    }
    result = wndproc(h, m, w, l);
    if (m == reply_message && displayed_generation == view_generation) {
        const auto it = refresh_starts.find(view_generation);
        if (it != refresh_starts.end()) {
            refresh_times.push_back(elapsed(it->second));
            refresh_starts.erase(it);
        }
    }
    const auto ms = elapsed(start);
    dispatch_times.push_back(ms);
    message_max[m] = std::max(message_max[m], ms);
    return result;
}
nm::Json timings(std::vector<double> values) {
    if (values.empty())
        return {{"count", 0}};
    std::sort(values.begin(), values.end());
    return {{"count", values.size()},
            {"p95_ms", values[(values.size() - 1) * 95 / 100]},
            {"max_ms", values.back()}};
}
} // namespace
int main(int argc, char **argv) {
    read_only_client = true;
    instance = GetModuleHandleW(nullptr);
    QueryPerformanceFrequency(&counter_frequency);
    // --live is read-only against the installed service. Default uses isolated demo.
    const bool live = argc > 1 && std::string(argv[1]) == "--live";
    pipe_name = live ? nm::service_pipe : nm::demo_pipe();
    const bool stress = argc > 1 && std::string(argv[1]) == "--stress";
    std::atomic<std::uint64_t> stress_revision{1};
    std::atomic<bool> first_query{true};
    if (stress)
        client_transport = [&](const nm::Json &q, HANDLE cancel) -> nm::Json {
            const auto op = q.value("op", "");
            if (op == "watch") {
                if (WaitForSingleObject(cancel, 5) == WAIT_OBJECT_0)
                    throw std::runtime_error("Cancelled");
                return {{"revision", ++stress_revision}};
            }
            if (op == "status")
                return {{"demo", true},
                        {"revision", stress_revision.load()},
                        {"config_revision", 1},
                        {"collector", {{"subscribed", true}, {"message", "Stress test"}}}};
            if (op == "config")
                return nm::Config{};
            if (op == "query") {
                const auto delay = first_query.exchange(false) ? 1500 : 15;
                if (WaitForSingleObject(cancel, static_cast<DWORD>(delay)) == WAIT_OBJECT_0) {
                    ++stress_cancellations;
                    throw std::runtime_error("Cancelled");
                }
                nm::Json data = nm::Json::array();
                for (int i = 0; i < 200; ++i) {
                    nm::Event event;
                    event.id = std::to_string(i);
                    event.app = "C:\\\\Test\\\\client.exe";
                    event.pid = static_cast<std::uint32_t>(i);
                    event.time = nm::now_ms();
                    event.remote_ip = i % 3 == 0   ? "8.8.8.8"
                                      : i % 3 == 1 ? "1.1.1.1"
                                                   : "133.242.0.1";
                    event.remote_port = 443;
                    data.push_back(event);
                }
                return data;
            }
            throw std::runtime_error("Unexpected stress operation");
        };
    INITCOMMONCONTROLSEX controls{sizeof(controls),
                                  ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = measured_proc;
    cls.lpszClassName = L"NetMonResponsivenessTest";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&cls);
    const auto h = CreateWindowW(cls.lpszClassName, L"NetMon responsiveness test",
                                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                 CW_USEDEFAULT, 1280, 880, nullptr, nullptr, instance, nullptr);
    if (!h)
        return 2;
    ShowWindow(h, SW_SHOWNOACTIVATE);
    SetTimer(h, exercise_timer, 250, nullptr);
    std::thread probe([h] {
        const auto end = Clock::now() + std::chrono::seconds(15);
        while (Clock::now() < end) {
            PostMessageW(h, ping_message, counter(), 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        PostMessageW(h, finish_message, 0, 0);
    });
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        dispatch(msg);
    }
    probe.join();
    diagnostics.reset();
    nm::Json slow = nm::Json::array();
    for (const auto &[id, ms] : message_max)
        if (ms >= 10)
            slow.push_back({{"message", id}, {"max_ms", ms}});
    nm::Json groups = nm::Json::object();
    for (const auto &[name, values] : action_groups)
        groups[name] = timings(values);
    std::cout << nm::Json{{"live_service", live},
                          {"stress", stress},
                          {"cancelled_stale_queries", stress_cancellations.load()},
                          {"action_groups", groups},
                          {"window_handlers", timings(dispatch_times)},
                          {"posted_message_latency", timings(queue_times)},
                          {"control_actions", timings(action_times)},
                          {"tab_checks", tab_checks},
                          {"view_result_latency", timings(refresh_times)},
                          {"view_changes", view_changes},
                          {"selections", selections},
                          {"slow_handlers", slow},
                          {"scope", "Complete visible production window; internal control actions, "
                                    "read-only service requests; no injected mouse input"}}
                     .dump(2)
              << "\n";
    return tab_checks == view_changes && view_changes > 0 && selections > 0 &&
                   !refresh_times.empty() && !queue_times.empty() &&
                   *std::max_element(queue_times.begin(), queue_times.end()) < 100 &&
                   *std::max_element(action_times.begin(), action_times.end()) < 100
               ? 0
               : 1;
}
