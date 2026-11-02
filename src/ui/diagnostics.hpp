#pragma once
#include "platform/windows.hpp"
#include <atomic>
#include <array>
#include <mutex>
#include <thread>
namespace nm::ui {
class Diagnostics {
  public:
    struct Dispatch {
        std::uint64_t start = 0, serial = 0;
        UINT message = 0;
        HWND target = nullptr;
        DWORD queue_ms = 0;
        bool input = false;
    };
    Diagnostics(HWND window, UINT heartbeat_message, std::filesystem::path path);
    ~Diagnostics();
    Dispatch begin(const MSG &message);
    void end(const Dispatch &dispatch);
    void heartbeat();
    static std::filesystem::path default_path();

  private:
    struct Sample {
        Millis time;
        UINT message;
        int control;
        double queue_ms, handler_ms;
    };
    HWND window_;
    UINT heartbeat_message_;
    DWORD ui_thread_;
    std::filesystem::path path_;
    Handle stop_;
    std::thread writer_;
    std::uint64_t frequency_, started_;
    std::atomic<std::uint64_t> dispatch_serial_{0}, dispatch_count_{0}, input_count_{0};
    std::atomic<std::uint64_t> max_input_ms_{0}, max_dispatch_ticks_{0}, max_heartbeat_ticks_{0};
    std::atomic<std::uint64_t> active_start_{0}, ping_start_{0};
    std::atomic<UINT> active_message_{0};
    std::mutex samples_mutex_;
    std::array<Sample, 32> samples_{};
    std::size_t samples_count_ = 0, samples_next_ = 0;
    static std::uint64_t counter();
    static void maximum(std::atomic<std::uint64_t> &value, std::uint64_t candidate);
    void run() noexcept;
};
} // namespace nm::ui
