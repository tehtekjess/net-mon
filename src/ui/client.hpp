#pragma once
#include "ui/table.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
namespace nm::ui {
struct ViewRequest {
    std::string view = "live", search;
    int offset = 0;
    std::uint64_t generation = 0;
};
struct ViewResult {
    std::uint64_t generation = 0;
    std::shared_ptr<const TableSnapshot> snapshot;
    std::wstring error;
};
struct StatusText {
    std::wstring summary, tip;
    bool healthy = false, demo = false;
    std::uint64_t processed = 0, storage_bytes = 0, dropped = 0;
};
struct AlertText {
    std::wstring title, body;
};
struct Completion {
    std::wstring label, error;
};
struct UiUpdate {
    std::optional<ViewResult> view;
    std::optional<bool> dark_theme;
    std::optional<StatusText> status;
    std::shared_ptr<const Config> config;
    std::optional<AlertText> alert;
    std::vector<Completion> completions;
};
// UI methods never perform IPC, file I/O, parsing, or wait for a worker result.
class Client {
  public:
    using Transport = std::function<Json(const Json &, HANDLE)>;
    Client(std::wstring pipe, std::function<void()> wake, bool read_only = false,
           Transport transport = {});
    ~Client();
    void set_view(ViewRequest request);
    void set_visible(bool visible);
    void submit(Json request);
    void save_theme(bool dark);
    void import_file(std::filesystem::path path, bool feed);
    void export_records(std::filesystem::path path, std::shared_ptr<const TableSnapshot> records);
    void export_rules(std::filesystem::path path, std::shared_ptr<const Config> config);
    bool take(UiUpdate &update); // false means busy; retry later instead of blocking input.
    void stop();

  private:
    struct Job {
        std::wstring label;
        std::function<void(HANDLE)> execute;
    };
    Transport transport_;
    std::function<void()> wake_;
    bool read_only_;
    Handle stop_event_, view_cancel_;
    std::atomic<bool> stopped_{false};
    std::atomic<unsigned> outstanding_{0};
    std::mutex state_mutex_, mailbox_mutex_;
    std::condition_variable view_cv_, command_cv_;
    ViewRequest desired_;
    bool view_dirty_ = true, visible_ = false;
    std::deque<Job> jobs_;
    UiUpdate pending_;
    bool posted_ = false;
    std::thread events_, views_, commands_;
    Json request(const Json &, HANDLE);
    void publish(UiUpdate update);
    void refresh();
    void queue(Job job);
    void event_loop();
    void view_loop();
    void command_loop();
};
} // namespace nm::ui
