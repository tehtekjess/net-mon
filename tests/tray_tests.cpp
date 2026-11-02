#include "ui/tray.hpp"
#include "core/model.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
using Clock = std::chrono::steady_clock;
int checks = 0;
void check(bool value, const char *reason) {
    ++checks;
    if (!value)
        throw std::runtime_error(reason);
}
struct Call {
    DWORD operation;
    NOTIFYICONDATAW data;
    DWORD thread;
};
struct Recorder {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<Call> calls;
    bool entered = false, release = false;
    int failures = 0;
    BOOL call(DWORD operation, PNOTIFYICONDATAW data) {
        std::unique_lock<std::mutex> lock(mutex);
        calls.push_back({operation, *data, GetCurrentThreadId()});
        if (operation == NIM_ADD && !entered) {
            entered = true;
            changed.notify_all();
            changed.wait_for(lock, std::chrono::seconds(5), [&] { return release; });
        }
        changed.notify_all();
        if (operation == NIM_ADD && failures > 0) {
            --failures;
            return FALSE;
        }
        return TRUE;
    }
    bool wait(const std::function<bool()> &ready) {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(3), ready);
    }
    std::size_t count(DWORD operation) const {
        std::size_t n = 0;
        for (const auto &c : calls)
            if (c.operation == operation)
                ++n;
        return n;
    }
};
} // namespace
int main() {
    try {
        Recorder r;
        const auto ui_thread = GetCurrentThreadId();
        double enqueue_ms = 0;
        {
            nm::ui::TrayIcon tray(nullptr, WM_APP + 2, [&](DWORD op, PNOTIFYICONDATAW data) {
                return r.call(op, data);
            });
            check(r.wait([&] { return r.entered; }), "Shell worker did not start");
            const auto start = Clock::now();
            for (int i = 0; i < 1000; ++i) {
                tray.set_tip(L"Status " + std::to_wstring(i));
                tray.notify(L"Alert " + std::to_wstring(i), L"Test body");
            }
            enqueue_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            {
                std::lock_guard<std::mutex> lock(r.mutex);
                r.release = true;
            }
            r.changed.notify_all();
            check(enqueue_ms < 100, "Blocked shell stalled producers");
            check(r.wait([&] { return r.count(NIM_MODIFY) >= 2; }),
                  "Pending latest updates not delivered");
            {
                std::lock_guard<std::mutex> lock(r.mutex);
                check(r.count(NIM_ADD) == 1, "Unexpected duplicate add");
                check(r.count(NIM_SETVERSION) == 1, "Tray version not negotiated");
                check(r.count(NIM_MODIFY) == 2, "Pending updates were not coalesced");
                bool tip = false, balloon = false, worker = true;
                for (const auto &c : r.calls) {
                    worker = worker && c.thread != ui_thread;
                    if (c.operation == NIM_MODIFY && c.data.uFlags == NIF_TIP)
                        tip = std::wstring(c.data.szTip) == L"Status 999";
                    if (c.operation == NIM_MODIFY && c.data.uFlags == NIF_INFO)
                        balloon = std::wstring(c.data.szInfoTitle) == L"Alert 999";
                }
                check(worker, "Shell call executed on producer thread");
                check(tip, "Latest tooltip lost");
                check(balloon, "Latest alert lost");
            }
            for (int i = 0; i < 1000; ++i)
                tray.set_tip(L"Status 999");
            tray.recreate();
            check(r.wait([&] { return r.count(NIM_SETVERSION) >= 2; }),
                  "Explorer restart not handled");
        }
        {
            std::lock_guard<std::mutex> lock(r.mutex);
            check(r.count(NIM_DELETE) == 1, "Shutdown did not remove tray icon");
            check(r.count(NIM_MODIFY) == 2, "Unchanged tooltip was sent again");
        }
        Recorder failed;
        failed.release = true;
        failed.failures = 1;
        {
            nm::ui::TrayIcon tray(nullptr, WM_APP + 2, [&](DWORD op, PNOTIFYICONDATAW data) {
                return failed.call(op, data);
            });
            check(failed.wait([&] { return failed.entered; }), "Failed add not attempted");
            tray.recreate();
            check(failed.wait([&] { return failed.count(NIM_SETVERSION) == 1; }),
                  "Could not recover from failed add");
        }
        std::cout << nm::Json{{"passed", true},
                              {"checks", checks},
                              {"queued_updates", 2000},
                              {"enqueue_while_shell_blocked_ms", enqueue_ms}}
                         .dump(2)
                  << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
}
