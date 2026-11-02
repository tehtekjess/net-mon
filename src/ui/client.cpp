#include "ui/client.hpp"
#include "ipc/pipe.hpp"
#include "platform/geo.hpp"
#include <chrono>
#include <algorithm>
#include <utility>
namespace nm::ui {
namespace {
std::filesystem::path preferences_path() {
    wchar_t root[32768]{};
    const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", root, 32768);
    if (!size || size >= 32768)
        throw std::runtime_error("Local preferences directory unavailable");
    return std::filesystem::path(root) / L"NetMon" / L"ui-preferences.json";
}
StatusText status_text(const Json &s) {
    const bool demo = s.value("demo", false);
    const bool healthy = s.at("collector").value("subscribed", false) &&
                         (demo || s.value("audit_enabled", false)) &&
                         s.value("storage_error", "").empty();
    std::string line = demo ? "DEMO | " : healthy ? "Monitoring | " : "Attention required | ";
    line += s.at("collector").value("message", "");
    line += "\r\nProcessed " + std::to_string(s.value("processed", 0ULL)) + " | Dropped " +
            std::to_string(s.value("dropped", 0ULL)) + " | History " +
            std::to_string(s.value("storage_bytes", 0ULL) / 1048576) +
            " MiB | Reputation: " + s.value("feed_status", "Not configured") +
            " | Country estimates: offline DB-IP 2026-11";
    if (!demo && !s.value("audit_enabled", false))
        line += " | WFP auditing is disabled";
    if (!s.value("storage_error", "").empty())
        line += " | " + s.value("storage_error", "");
    if (!s.value("review_network_error", "").empty())
        line += " | " + s.value("review_network_error", "");
    return {wide(line),
            demo      ? L"NetMon - demo"
            : healthy ? L"NetMon - monitoring"
                      : L"NetMon - attention required",
            healthy,
            demo,
            s.value("processed", 0ULL),
            s.value("storage_bytes", 0ULL),
            s.value("dropped", 0ULL)};
}
} // namespace
Client::Client(std::wstring pipe, std::function<void()> wake, bool read_only, Transport transport)
    : transport_(std::move(transport)), wake_(std::move(wake)), read_only_(read_only),
      stop_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      view_cancel_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    if (!stop_event_ || !view_cancel_)
        throw std::runtime_error(win_error());
    if (!transport_)
        transport_ = [pipe = std::move(pipe)](const Json &q, HANDLE stop) {
            return call_pipe(pipe, q, stop);
        };
    try {
        events_ = std::thread([this] { event_loop(); });
        views_ = std::thread([this] { view_loop(); });
        commands_ = std::thread([this] { command_loop(); });
    } catch (...) {
        stop();
        throw;
    }
}
Client::~Client() { stop(); }
void Client::stop() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        stopped_ = true;
        SetEvent(stop_event_.get());
        SetEvent(view_cancel_.get());
    }
    view_cv_.notify_all();
    command_cv_.notify_all();
    if (events_.joinable())
        events_.join();
    if (views_.joinable())
        views_.join();
    if (commands_.joinable())
        commands_.join();
}
Json Client::request(const Json &q, HANDLE cancel) {
    const auto op = q.value("op", "");
    if (read_only_) {
        if (op == "notifications")
            return Json::array();
        if (op != "watch" && op != "status" && op != "config" && op != "query")
            throw std::runtime_error("Read-only client");
    }
    return transport_(q, cancel);
}
void Client::publish(UiUpdate update) {
    bool signal = false;
    {
        std::lock_guard<std::mutex> lock(mailbox_mutex_);
        if (update.dark_theme)
            pending_.dark_theme = update.dark_theme;
        if (update.view)
            pending_.view = std::move(update.view);
        if (update.status)
            pending_.status = std::move(update.status);
        if (update.config)
            pending_.config = std::move(update.config);
        if (update.alert)
            pending_.alert = std::move(update.alert);
        for (auto &done : update.completions)
            pending_.completions.push_back(std::move(done));
        if (!posted_) {
            posted_ = true;
            signal = true;
        }
    }
    if (signal && !stopped_)
        wake_();
}
bool Client::take(UiUpdate &update) {
    std::unique_lock<std::mutex> lock(mailbox_mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    UiUpdate incoming;
    std::swap(incoming, pending_);
    posted_ = false;
    lock.unlock();
    outstanding_.fetch_sub(static_cast<unsigned>(incoming.completions.size()));
    update = std::move(incoming);
    return true;
}
void Client::set_view(ViewRequest value) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        desired_ = std::move(value);
        view_dirty_ = true;
        SetEvent(view_cancel_.get());
    }
    view_cv_.notify_one();
}
void Client::set_visible(bool visible) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (visible_ == visible)
            return;
        visible_ = visible;
        view_dirty_ = true;
        if (!visible)
            SetEvent(view_cancel_.get());
    }
    view_cv_.notify_one();
}
void Client::refresh() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        view_dirty_ = true;
    }
    view_cv_.notify_one();
}
void Client::queue(Job job) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (stopped_)
            throw std::runtime_error("Client is stopping");
        if (outstanding_.load() >= 32)
            throw std::runtime_error("Please wait for pending actions to finish");
        jobs_.push_back(std::move(job));
        ++outstanding_;
    }
    command_cv_.notify_one();
}
void Client::save_theme(bool dark) {
    if (read_only_)
        return;
    queue({L"Appearance saved", [dark](HANDLE) {
               const auto path = preferences_path();
               std::filesystem::create_directories(path.parent_path());
               atomic_write(path, Json{{"version", 1}, {"dark_theme", dark}}.dump());
           }});
}
void Client::submit(Json q) {
    queue({L"Saved", [this, q = std::move(q)](HANDLE stop) { request(q, stop); }});
}
void Client::import_file(std::filesystem::path path, bool feed) {
    queue({L"Imported", [this, path = std::move(path), feed](HANDLE stop) {
               auto data = Json::parse(read_text(path));
               request(feed ? Json{{"op", "feed"}, {"feed", std::move(data)}}
                            : Json{{"op", "import_rules"}, {"rules", data.at("rules")}},
                       stop);
           }});
}
void Client::export_records(std::filesystem::path path,
                            std::shared_ptr<const TableSnapshot> records) {
    queue({L"Exported", [path = std::move(path), records = std::move(records)](HANDLE) {
               atomic_write(path, records->records.dump(2));
           }});
}
void Client::export_rules(std::filesystem::path path, std::shared_ptr<const Config> config) {
    queue({L"Exported", [path = std::move(path), config = std::move(config)](HANDLE) {
               atomic_write(path, Json{{"version", 1}, {"rules", config->rules}}.dump(2));
           }});
}
void Client::command_loop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(state_mutex_);
            command_cv_.wait(lock, [this] { return stopped_ || !jobs_.empty(); });
            if (stopped_)
                return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        Completion result{job.label, {}};
        try {
            job.execute(stop_event_.get());
            refresh();
        } catch (const std::exception &e) {
            result.error = wide(e.what());
        }
        UiUpdate update;
        update.completions.push_back(std::move(result));
        publish(std::move(update));
    }
}
void Client::event_loop() {
    if (!read_only_) {
        try {
            const auto prefs = Json::parse(read_text(preferences_path(), 4096));
            UiUpdate update;
            update.dark_theme = prefs.at("dark_theme").get<bool>();
            publish(std::move(update));
        } catch (...) {
        }
    }
    std::uint64_t revision = 0, config_revision = UINT64_MAX;
    Millis service_started = -1;
    auto health_due = std::chrono::steady_clock::now();
    auto updates_due = health_due;
    bool pending_change = true;
    for (; !stopped_;) {
        try {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       updates_due - std::chrono::steady_clock::now())
                                       .count();
            const auto timeout =
                pending_change ? static_cast<int>(std::clamp<std::int64_t>(remaining, 1, 1000))
                               : 1000;
            const auto event = request(
                {{"op", "watch"}, {"after", revision}, {"timeout_ms", timeout}}, stop_event_.get());
            const auto next = event.at("revision").get<std::uint64_t>();
            const bool changed = next != revision;
            revision = next;
            if (changed) {
                refresh();
                pending_change = true;
            }
            if ((pending_change && std::chrono::steady_clock::now() >= updates_due) ||
                std::chrono::steady_clock::now() >= health_due) {
                const auto status = request({{"op", "status"}}, stop_event_.get());
                if (service_started != status.value("started", Millis{0})) {
                    service_started = status.value("started", Millis{0});
                    config_revision = UINT64_MAX;
                    refresh();
                }
                UiUpdate update;
                update.status = status_text(status);
                const auto cfg_revision = status.value("config_revision", 0ULL);
                if (cfg_revision != config_revision) {
                    update.config = std::make_shared<const Config>(
                        request({{"op", "config"}}, stop_event_.get()).get<Config>());
                    config_revision = cfg_revision;
                }
                // Publish status/config before any alert queue operation.
                publish(std::move(update));
                health_due = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                updates_due = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
                if (pending_change) {
                    const auto alerts = request({{"op", "notifications"}}, stop_event_.get());
                    if (!alerts.empty()) {
                        const auto e = alerts.back().get<Event>();
                        UiUpdate alert;
                        auto body = wide(e.reason);
                        if (alerts.size() > 1)
                            body += L" (" + std::to_wstring(alerts.size()) + L" new alerts)";
                        alert.alert =
                            AlertText{std::filesystem::path(wide(e.app)).filename().wstring() +
                                          L" -> " + wide(e.remote_ip),
                                      std::move(body)};
                        publish(std::move(alert));
                    }
                }
                pending_change = false;
            }
        } catch (const std::exception &e) {
            if (stopped_)
                return;
            UiUpdate update;
            update.status = StatusText{wide(e.what()), L"NetMon - disconnected"};
            publish(std::move(update));
            // Reset cursors after reconnect; revision values can repeat after restart.
            revision = 0;
            config_revision = UINT64_MAX;
            if (WaitForSingleObject(stop_event_.get(), 500) == WAIT_OBJECT_0)
                return;
        }
    }
}
void Client::view_loop() {
    GeoDatabase countries;
    std::shared_ptr<const WorldMap> world;
    bool countries_attempted = false;
    Json previous;
    ViewRequest previous_view;
    bool have_previous = false;
    auto last_refresh = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    for (;;) {
        ViewRequest wanted;
        {
            std::unique_lock<std::mutex> lock(state_mutex_);
            view_cv_.wait(lock, [this] { return stopped_ || (visible_ && view_dirty_); });
            if (stopped_)
                return;
            // Bound monitoring refreshes to 10/sec; explicit view/search changes bypass this.
            if (have_previous && desired_.generation == previous_view.generation)
                view_cv_.wait_until(
                    lock, last_refresh + std::chrono::milliseconds(100), [this, &previous_view] {
                        return stopped_ || desired_.generation != previous_view.generation;
                    });
            if (stopped_)
                return;
            if (!visible_)
                continue;
            wanted = desired_;
            view_dirty_ = false;
            ResetEvent(view_cancel_.get());
        }
        if (!countries_attempted) {
            countries_attempted = true;
            try {
                countries.load(executable_path().parent_path() / L"data" / L"geoip-country.csv");
            } catch (...) {
            } // Offline lookup labels the missing database explicitly.
            try {
                auto data = std::make_shared<WorldMap>();
                data->load(executable_path().parent_path() / L"data" / L"world-map.json");
                world = std::move(data);
            } catch (...) {
            }
        }
        ViewResult result;
        result.generation = wanted.generation;
        try {
            const bool rules = wanted.view == "rules";
            Json values = rules ? request({{"op", "config"}}, view_cancel_.get()).at("rules")
                                : request({{"op", "query"},
                                           {"view", wanted.view == "map" ? "live" : wanted.view},
                                           {"search", wanted.search},
                                           {"offset", wanted.offset}},
                                          view_cancel_.get());
            if (have_previous && wanted.generation == previous_view.generation &&
                wanted.view == previous_view.view && values == previous) {
                last_refresh = std::chrono::steady_clock::now();
                continue;
            }
            previous = values;
            previous_view = wanted;
            have_previous = true;
            for (auto &item : values) {
                const auto country = countries.lookup_country(item.value("remote_ip", ""));
                item["country"] = country.label;
                item["country_code"] = country.code;
            }
            result.snapshot = std::make_shared<const TableSnapshot>(
                prepare_table(std::move(values), rules, world));
        } catch (const std::exception &e) {
            result.error = wide(e.what());
        }
        last_refresh = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (stopped_)
                return;
            if (!visible_ || wanted.generation != desired_.generation)
                continue;
        }
        UiUpdate update;
        update.view = std::move(result);
        publish(std::move(update));
    }
}
} // namespace nm::ui
