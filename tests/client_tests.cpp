#include "ui/client.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace {
int checks = 0;
void check(bool good, const char *reason) {
    ++checks;
    if (!good)
        throw std::runtime_error(reason);
}
template <class F> bool until(F test, int milliseconds = 4000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        if (test())
            return true;
        Sleep(5);
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
} // namespace
int main() {
    try {
        std::atomic<unsigned> wakes{0}, queries{0}, cancelled{0};
        std::atomic<bool> blocked{false}, command_started{false};
        std::atomic<std::uint64_t> revision{1};
        nm::Handle release(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        std::mutex order_mutex;
        std::vector<int> order;
        nm::ui::Client client(
            L"unused", [&] { ++wakes; }, false,
            [&](const nm::Json &q, HANDLE cancel) -> nm::Json {
                const auto op = q.value("op", "");
                if (op == "watch") {
                    WaitForSingleObject(cancel, 5);
                    if (WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0)
                        throw std::runtime_error("Cancelled");
                    return {{"revision", revision.load()}};
                }
                if (op == "status")
                    return {{"demo", true},
                            {"config_revision", 1},
                            {"collector", {{"subscribed", true}, {"message", "Test"}}}};
                if (op == "config")
                    return nm::Config{};
                if (op == "notifications")
                    return nm::Json::array();
                if (op == "query") {
                    if (++queries == 1) {
                        blocked = true;
                        if (WaitForSingleObject(cancel, 3000) == WAIT_OBJECT_0)
                            ++cancelled;
                        // Deliberately return an obsolete response even after cancellation.
                    }
                    nm::Event e;
                    e.id = q.value("search", "first");
                    e.app = "C:\\Test\\client.exe";
                    e.remote_ip = "203.0.113.1";
                    return nm::Json::array({e});
                }
                if (op == "mark") {
                    command_started = true;
                    HANDLE waits[] = {cancel, release.get()};
                    if (WaitForMultipleObjects(2, waits, FALSE, 3000) != WAIT_OBJECT_0 + 1)
                        throw std::runtime_error("Cancelled command");
                    std::lock_guard<std::mutex> lock(order_mutex);
                    order.push_back(q.at("sequence").get<int>());
                    return {{"saved", true}};
                }
                throw std::runtime_error("Unexpected operation");
            });
        client.set_visible(true);
        check(until([&] { return blocked.load(); }), "Initial query did not enter the delay");
        nm::ui::UiUpdate update;
        check(until([&] { return client.take(update) && update.config != nullptr; }),
              "Status/config were blocked behind the delayed view");
        update = {};
        const auto switch_start = std::chrono::steady_clock::now();
        client.set_view({"review", "new", 0, 2});
        std::uint64_t delivered = 0;
        check(until(
                  [&] {
                      nm::ui::UiUpdate next;
                      if (client.take(next) && next.view && next.view->snapshot) {
                          delivered = next.view->generation;
                          return delivered == 2 &&
                                 next.view->snapshot->records[0].at("id") == "new";
                      }
                      return false;
                  },
                  1000),
              "Replacement view was blocked or stale");
        check(cancelled == 1, "Superseded request did not receive cancellation");
        const auto switch_ms = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - switch_start)
                                   .count();
        check(switch_ms < 1000, "View replacement exceeded the cancellation budget");
        client.submit({{"op", "mark"}, {"sequence", 0}});
        check(until([&] { return command_started.load(); }), "Command did not start");
        client.set_view({"live", "during-save", 0, 3});
        check(until(
                  [&] {
                      nm::ui::UiUpdate next;
                      return client.take(next) && next.view && next.view->snapshot &&
                             next.view->generation == 3;
                  },
                  1000),
              "Blocked command stalled independent view retrieval");
        for (int i = 1; i < 32; ++i)
            client.submit({{"op", "mark"}, {"sequence", i}});
        bool rejected = false;
        try {
            client.submit({{"op", "mark"}, {"sequence", 32}});
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Command queue did not enforce its bound");
        SetEvent(release.get());
        std::size_t completed = 0;
        check(until([&] {
                  nm::ui::UiUpdate next;
                  if (client.take(next)) {
                      for (const auto &done : next.completions)
                          if (!done.error.empty())
                              throw std::runtime_error("Command failed unexpectedly");
                      completed += next.completions.size();
                  }
                  return completed == 32;
              }),
              "Command acknowledgements were lost");
        {
            std::lock_guard<std::mutex> lock(order_mutex);
            check(order.size() == 32, "Wrong number of commands executed");
            for (int i = 0; i < 32; ++i)
                check(order[static_cast<std::size_t>(i)] == i, "Commands reordered");
        }
        nm::ui::UiUpdate clear;
        client.take(clear);
        const auto before = wakes.load();
        for (int i = 0; i < 1000; ++i)
            client.set_view({"live", std::to_string(i), 0, 100 + static_cast<std::uint64_t>(i)});
        ++revision;
        Sleep(250);
        check(wakes.load() <= before + 1, "Display wakeups accumulated instead of coalescing");
        nm::ui::UiUpdate latest;
        check(client.take(latest) && latest.view && latest.view->generation == 1099,
              "Mailbox did not retain the latest view");
        const auto shutdown = std::chrono::steady_clock::now();
        client.stop();
        const auto shutdown_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shutdown)
                .count();
        check(shutdown_ms < 1000, "Shutdown did not cancel pending requests");
        std::cout << nm::Json{{"passed", true},
                              {"checks", checks},
                              {"view_replacement_ms", switch_ms},
                              {"shutdown_ms", shutdown_ms},
                              {"completed_commands", completed},
                              {"coalesced_view_requests", 1000}}
                         .dump(2)
                  << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
}
