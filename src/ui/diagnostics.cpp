#include "ui/diagnostics.hpp"
#include <wct.h>
#include <algorithm>
namespace nm::ui {
std::uint64_t Diagnostics::counter() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}
void Diagnostics::maximum(std::atomic<std::uint64_t> &value, std::uint64_t candidate) {
    auto old = value.load();
    while (old < candidate && !value.compare_exchange_weak(old, candidate)) {
    }
}
std::filesystem::path Diagnostics::default_path() {
    wchar_t root[32768]{};
    const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", root, 32768);
    if (!count || count >= 32768)
        throw std::runtime_error("LocalAppData is unavailable");
    return std::filesystem::path(root) / L"NetMon" / L"ui-diagnostics.json";
}
Diagnostics::Diagnostics(HWND window, UINT message, std::filesystem::path path)
    : window_(window), heartbeat_message_(message), ui_thread_(GetCurrentThreadId()),
      path_(std::move(path)), stop_(CreateEventW(nullptr, TRUE, FALSE, nullptr)), frequency_(0),
      started_(counter()) {
    if (!stop_)
        throw std::runtime_error(win_error());
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    frequency_ = static_cast<std::uint64_t>(frequency.QuadPart);
    writer_ = std::thread([this] { run(); });
}
Diagnostics::~Diagnostics() {
    SetEvent(stop_.get());
    if (writer_.joinable())
        writer_.join();
}
Diagnostics::Dispatch Diagnostics::begin(const MSG &message) {
    Dispatch result;
    result.start = counter();
    result.serial = ++dispatch_serial_;
    result.message = message.message;
    result.target = message.hwnd;
    result.input = (message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST) ||
                   (message.message >= WM_MOUSEFIRST && message.message <= WM_MOUSELAST) ||
                   (message.message >= WM_NCMOUSEMOVE && message.message <= WM_NCXBUTTONDBLCLK);
    if (result.input) {
        result.queue_ms = GetTickCount() - message.time;
        // Ignore timestamps older than an hour (e.g. deliberately posted test messages).
        if (result.queue_ms > 3600000)
            result.queue_ms = 0;
        ++input_count_;
        maximum(max_input_ms_, result.queue_ms);
    }
    ++dispatch_count_;
    active_message_ = message.message;
    active_start_ = result.start;
    return result;
}
void Diagnostics::end(const Dispatch &dispatch) {
    const auto duration = counter() - dispatch.start;
    // A nested application modal pump is not a single blocking handler.
    const bool leaf = dispatch_serial_.load() == dispatch.serial;
    if (leaf)
        maximum(max_dispatch_ticks_, duration);
    const auto ms = static_cast<double>(duration) * 1000.0 / static_cast<double>(frequency_);
    if ((leaf && ms >= 50.0) || (dispatch.input && dispatch.queue_ms >= 50)) {
        std::unique_lock<std::mutex> lock(samples_mutex_, std::try_to_lock);
        if (lock.owns_lock()) {
            samples_[samples_next_] = {now_ms(), dispatch.message, GetDlgCtrlID(dispatch.target),
                                       static_cast<double>(dispatch.queue_ms), leaf ? ms : 0.0};
            samples_next_ = (samples_next_ + 1) % samples_.size();
            samples_count_ = std::min(samples_count_ + 1, samples_.size());
        }
    }
    active_start_ = 0;
    active_message_ = 0;
}
void Diagnostics::heartbeat() {
    const auto start = ping_start_.exchange(0);
    if (start)
        maximum(max_heartbeat_ticks_, counter() - start);
}
void Diagnostics::run() noexcept {
    Json wait_chain = Json::array();
    std::uint64_t last_write = 0, last_wait = 0;
    auto session = OpenThreadWaitChainSession(0, nullptr);
    try {
        std::filesystem::create_directories(path_.parent_path());
        if (std::filesystem::exists(path_) && std::filesystem::file_size(path_) <= 65536)
            atomic_write(path_.parent_path() / L"ui-diagnostics.previous.json",
                         read_text(path_, 65536));
    } catch (...) {
    }
    for (;;) {
        const bool stop = WaitForSingleObject(stop_.get(), 250) == WAIT_OBJECT_0;
        const auto now = counter();
        const auto ping = ping_start_.load();
        if (ping) {
            maximum(max_heartbeat_ticks_, now - ping);
            if (session && now - ping > frequency_ / 10 && now - last_wait > frequency_) {
                last_wait = now;
                WAITCHAIN_NODE_INFO nodes[WCT_MAX_NODE_COUNT]{};
                DWORD count = WCT_MAX_NODE_COUNT;
                BOOL cycle = FALSE;
                wait_chain = Json::array();
                if (GetThreadWaitChain(session, 0, WCT_OUT_OF_PROC_FLAG, ui_thread_, &count, nodes,
                                       &cycle)) {
                    for (DWORD i = 0; i < count; ++i) {
                        Json node = {{"type", nodes[i].ObjectType},
                                     {"status", nodes[i].ObjectStatus}};
                        if (nodes[i].ObjectType == WctThreadType) {
                            node["pid"] = nodes[i].ThreadObject.ProcessId;
                            node["tid"] = nodes[i].ThreadObject.ThreadId;
                        }
                        wait_chain.push_back(std::move(node));
                    }
                }
            }
        } else if (!stop) {
            ping_start_ = now;
            if (!PostMessageW(window_, heartbeat_message_, 0, 0))
                ping_start_ = 0;
        }
        if (stop || now - last_write >= frequency_ * 5) {
            last_write = now;
            try {
                Json samples = Json::array();
                {
                    std::lock_guard<std::mutex> lock(samples_mutex_);
                    for (std::size_t i = 0; i < samples_count_; ++i) {
                        const auto &s = samples_[i];
                        samples.push_back({{"time", s.time},
                                           {"message", s.message},
                                           {"control", s.control},
                                           {"input_queue_ms", s.queue_ms},
                                           {"handler_ms", s.handler_ms}});
                    }
                }
                const auto ms = [this](std::uint64_t ticks) {
                    return static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency_);
                };
                const auto active = active_start_.load();
                const Json report = {
                    {"version", "0.5.0"},
                    {"process_id", GetCurrentProcessId()},
                    {"ui_thread_id", ui_thread_},
                    {"updated", now_ms()},
                    {"uptime_ms", ms(now - started_)},
                    {"dispatches", dispatch_count_.load()},
                    {"input_messages", input_count_.load()},
                    {"max_input_queue_ms", max_input_ms_.load()},
                    {"max_handler_ms", ms(max_dispatch_ticks_.load())},
                    {"max_heartbeat_ms", ms(max_heartbeat_ticks_.load())},
                    {"active_message", active_message_.load()},
                    {"active_handler_ms", active && now >= active ? ms(now - active) : 0.0},
                    {"recent_slow_handlers", std::move(samples)},
                    {"last_stall_wait_chain", wait_chain},
                    {"scope",
                     "Real message-loop timestamps; input age uses Windows tick resolution. No "
                     "keys, text, addresses or connection records are collected."}};
                const auto encoded = report.dump(2);
                if (encoded.size() <= 65536)
                    atomic_write(path_, encoded);
            } catch (...) {
            }
        }
        if (stop)
            break;
    }
    if (session)
        CloseThreadWaitChainSession(session);
}
} // namespace nm::ui
