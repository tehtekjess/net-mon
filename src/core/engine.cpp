#include "core/engine.hpp"
#include "platform/networks.hpp"
#include <algorithm>
#include <stdexcept>
namespace nm {
namespace {
void integer_bounds(const Json &object, const char *name, std::uint64_t maximum) {
    if (!object.contains(name))
        return;
    const auto &value = object.at(name);
    if (!value.is_number_integer() ||
        (!value.is_number_unsigned() && value.get<std::int64_t>() < 0) ||
        value.get<std::uint64_t>() > maximum)
        throw std::invalid_argument(std::string("Invalid numeric field: ") + name);
}
} // namespace
Engine::Engine(const std::filesystem::path &root) : root_(root) {
    std::filesystem::create_directories(root_);
    if (std::filesystem::exists(root_ / L"config.json"))
        config_ = Json::parse(read_text(root_ / L"config.json")).get<Config>();
    validate(config_);
    store_ = std::make_unique<Store>(root_, config_.settings);
    if (std::filesystem::exists(root_ / L"state.json")) {
        auto state = Json::parse(read_text(root_ / L"state.json"));
        checkpoint_ = state.value("bookmark", "");
        if (state.contains("seen"))
            seen_ = state.at("seen").get<std::unordered_map<std::string, Millis>>();
        if (seen_.size() > 8192)
            seen_.clear();
    }
    published_config_ = std::make_shared<const Config>(config_);
    publish();
    try {
        enrichment_worker_ = std::thread([this] { enrich(); });
        worker_ = std::thread([this] { run(); });
    } catch (...) {
        stop();
        throw;
    }
}
Engine::~Engine() { stop(); }
void Engine::stop() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stopped_.store(true);
    }
    { std::lock_guard<std::mutex> lock(watch_mutex_); }
    cv_.notify_all();
    capture_cv_.notify_all();
    changed_.notify_all();
    if (enrichment_worker_.joinable())
        enrichment_worker_.join();
    if (worker_.joinable())
        worker_.join();
}
std::string Engine::checkpoint() { return published_.load()->checkpoint; }
void Engine::enqueue(Event e, std::string bookmark) {
    const auto bytes = sizeof(Pending) + e.app.size() + bookmark.size() + 512;
    std::lock_guard<std::mutex> lock(queue_mutex_);
    ++received_;
    if (stopped_ || queue_bytes_.load() + bytes > 32 * 1024 * 1024) {
        ++dropped_;
        return;
    }
    queue_bytes_ += bytes;
    queue_.push_back({std::move(e), std::move(bookmark), bytes});
    capture_cv_.notify_one();
}
void Engine::save_config(const Config &c) {
    const auto data = Json(c).dump(2);
    if (data.size() > 1024 * 1024)
        throw std::runtime_error("Configuration limit is 1 MiB; remove unused rules/indicators");
    atomic_write(root_ / L"config.json", data);
}
void Engine::flush_state() {
    atomic_write(root_ / L"state.json", Json{{"bookmark", checkpoint_}, {"seen", seen_}}.dump());
    last_save_ = now_ms();
}
void Engine::publish() {
    auto next = std::make_shared<Published>();
    next->config = published_config_;
    next->recent = recent_;
    next->subnets = adapter_subnets_;
    next->checkpoint = checkpoint_;
    next->config_revision = config_revision_;
    next->status = {{"settings", config_.settings},
                    {"processed", processed_},
                    {"storage_bytes", store_->bytes()},
                    {"storage_error", storage_error_},
                    {"review_network_error", network_error_},
                    {"feed_status", config_.feed.indicators.empty()   ? "Not configured"
                                    : config_.feed.expires > now_ms() ? "Active"
                                                                      : "Expired"},
                    {"feed_count", config_.feed.indicators.size()},
                    {"started", started_}};
    {
        std::lock_guard<std::mutex> lock(watch_mutex_);
        next->revision = ++revision_;
        published_.store(std::move(next));
    }
    changed_.notify_all();
}
void Engine::enrich() noexcept {
    for (;;) {
        std::vector<Pending> batch;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            capture_cv_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
            if (queue_.empty() && stopped_)
                break;
            capture_cv_.wait_for(lock, std::chrono::milliseconds(50),
                                 [this] { return stopped_ || queue_.size() >= 128; });
            while (!queue_.empty() && batch.size() < 128) {
                batch.push_back(std::move(queue_.front()));
                queue_.pop_front();
            }
        }
        for (auto &item : batch) {
            try {
                enricher_.enrich(item.event);
            } catch (...) {
                item.event.signature = "unavailable";
            }
        }
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            for (auto &item : batch)
                ready_.push_back(std::move(item));
        }
        cv_.notify_one();
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        enrichment_done_ = true;
    }
    cv_.notify_all();
}
void Engine::run() noexcept {
    Millis last_network_refresh = 0;
    bool prefer_command = true;
    for (;;) {
        std::shared_ptr<Command> command;
        std::vector<Pending> batch;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait_for(lock, std::chrono::seconds(2), [this] {
                return !commands_.empty() || !ready_.empty() || (stopped_ && enrichment_done_);
            });
            if (!commands_.empty() && (ready_.empty() || prefer_command)) {
                prefer_command = false;
                command = std::move(commands_.front());
                commands_.pop_front();
            } else {
                prefer_command = true;
                while (!ready_.empty() && batch.size() < 128) {
                    queue_bytes_ -= ready_.front().bytes;
                    batch.push_back(std::move(ready_.front()));
                    ready_.pop_front();
                }
                if (batch.empty() && stopped_ && enrichment_done_)
                    break;
            }
        }
        if (command) {
            try {
                if (command->cancelled.load())
                    throw std::runtime_error("Query cancelled");
                auto result = request_owned(command->request, &command->cancelled);
                const auto op = command->request.value("op", "");
                if (op != "query" && op != "notifications") {
                    published_config_ = std::make_shared<const Config>(config_);
                    ++config_revision_;
                    for (auto &event : recent_)
                        classify(event, config_,
                                 recent_first_.find(event.id) != recent_first_.end(), now_ms());
                    publish();
                }
                command->result.set_value(std::move(result));
            } catch (...) {
                const auto error = std::current_exception();
                // A persisted setting may have succeeded before quota maintenance failed.
                // Publish the actual in-memory configuration even when completion reports an error.
                const auto op = command->request.value("op", "");
                if (op != "query" && op != "notifications") {
                    try {
                        published_config_ = std::make_shared<const Config>(config_);
                        ++config_revision_;
                        publish();
                    } catch (...) {
                    }
                }
                command->result.set_exception(error);
            }
            continue;
        }
        try {
            if (now_ms() - last_network_refresh > 30000) {
                last_network_refresh = now_ms();
                try {
                    adapter_subnets_ = adapter_ipv4_subnets();
                    network_error_.clear();
                } catch (const std::exception &e) {
                    network_error_ = e.what();
                }
                publish();
            }
            if (!batch.empty()) {
                std::vector<Event> retained;
                for (auto &item : batch) {
                    auto &event = item.event;
                    if (event.time < started_) {
                        const auto saved = store_->find(event.id);
                        if (saved) {
                            event = *saved;
                            continue;
                        }
                    }
                    item.first = seen_.find(match_key(event)) == seen_.end();
                    classify(event, config_, item.first, now_ms());
                    if (should_log(event, config_.settings))
                        retained.push_back(event);
                }
                store_->append_batch(retained);
                for (auto &item : batch) {
                    auto &event = item.event;
                    const auto key = match_key(event);
                    if (event.alert && event.time >= started_ &&
                        config_.settings.snooze_until <= now_ms()) {
                        const auto notification_key = key + event.classification + event.rule_id;
                        auto &last = cooldowns_[notification_key];
                        if (now_ms() - last >=
                            static_cast<Millis>(config_.settings.cooldown_seconds) * 1000) {
                            last = now_ms();
                            notifications_.push_back(event);
                            if (notifications_.size() > 100)
                                notifications_.pop_front();
                        }
                        if (cooldowns_.size() > 8192)
                            cooldowns_.clear();
                    }
                    seen_[key] = now_ms();
                    if (seen_.size() > 8192) {
                        const auto oldest = std::min_element(
                            seen_.begin(), seen_.end(),
                            [](const auto &a, const auto &b) { return a.second < b.second; });
                        seen_.erase(oldest);
                    }
                    recent_.push_front(event);
                    if (item.first)
                        recent_first_.insert(event.id);
                    if (recent_.size() > 200) {
                        recent_first_.erase(recent_.back().id);
                        recent_.pop_back();
                    }
                    checkpoint_ = std::move(item.bookmark);
                    ++processed_;
                }
                storage_error_.clear();
                publish();
            }
            if (now_ms() - last_save_ > 30000) {
                flush_state();
                store_->maintain();
                publish();
            }
        } catch (const std::exception &e) {
            storage_error_ = e.what();
            dropped_ += batch.size();
            try {
                publish();
            } catch (...) {
            }
        } catch (...) {
            storage_error_ = "Unexpected processing error";
            dropped_ += batch.size();
            try {
                publish();
            } catch (...) {
            }
        }
    }
    try {
        flush_state();
    } catch (...) {
    }
}
Json Engine::request(const Json &r, const std::function<bool()> &connected) {
    if (r.value("version", 1) != 1)
        throw std::invalid_argument("Unsupported IPC version");
    const auto op = r.at("op").get<std::string>();
    if (op == "watch") {
        integer_bounds(r, "after", UINT64_MAX);
        integer_bounds(r, "timeout_ms", 1000);
        const auto after = r.value("after", 0ULL);
        std::unique_lock<std::mutex> lock(watch_mutex_);
        changed_.wait_for(lock, std::chrono::milliseconds(r.value("timeout_ms", 1000)),
                          [&] { return stopped_ || revision_ != after; });
        return {{"revision", revision_}, {"stopped", stopped_.load()}};
    }
    const auto state = published_.load();
    if (op == "status") {
        auto result = state->status;
        result["received"] = received_.load();
        result["dropped"] = dropped_.load();
        result["queue_bytes"] = queue_bytes_.load();
        result["revision"] = state->revision;
        result["config_revision"] = state->config_revision;
        return result;
    }
    if (op == "config")
        return *state->config;
    if (op == "query") {
        const auto mode = r.value("view", "live");
        const auto search = lower_ascii(r.value("search", ""));
        if (search.size() > 256)
            throw std::invalid_argument("Search too long");
        if (mode == "live" || mode == "review") {
            Json out = Json::array();
            for (const auto &event : state->recent) {
                if (mode == "review" &&
                    ((event.classification != "unknown" && event.classification != "review") ||
                     excluded_from_review(event, state->config->settings, state->subnets)))
                    continue;
                if (lower_ascii(event.app + " " + event.remote_ip + " " + event.reason)
                        .find(search) != std::string::npos)
                    out.push_back(event);
            }
            return out;
        }
    }
    auto command = std::make_shared<Command>();
    command->request = r;
    auto result = command->result.get_future();
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stopped_)
            throw std::runtime_error("Service is stopping");
        if (commands_.size() >= 64)
            throw std::runtime_error("Service command queue is full");
        commands_.push_back(command);
    }
    cv_.notify_one();
    // Only abandon reads. An accepted mutation must finish even if its client disconnects.
    if (op == "query" && connected) {
        while (result.wait_for(std::chrono::milliseconds(25)) != std::future_status::ready) {
            if (!connected()) {
                command->cancelled = true;
                throw std::runtime_error("Query cancelled");
            }
        }
    }
    return result.get(); // IPC worker only; live reads never enter this queue.
}
Json Engine::request_owned(const Json &r, std::atomic<bool> *cancelled) {
    const auto op = r.at("op").get<std::string>();
    if (op == "query") {
        const auto search = r.value("search", "");
        return store_->query(search, r.value("offset", 0), r.value("limit", 100),
                             r.value("view", "history") == "alerts", cancelled);
    }
    if (op == "notifications") {
        Json out = Json::array();
        for (const auto &event : notifications_)
            out.push_back(event);
        notifications_.clear();
        return out;
    }
    if (op == "settings") {
        const auto &input = r.at("settings");
        integer_bounds(input, "history_mib", 4096);
        integer_bounds(input, "retention_days", 3650);
        integer_bounds(input, "cooldown_seconds", 86400);
        integer_bounds(input, "snooze_until",
                       static_cast<std::uint64_t>(now_ms() + 7LL * 86400000));
        Config next = config_;
        next.settings = input.get<Settings>();
        validate(next);
        save_config(next);
        config_ = std::move(next);
        store_->configure(config_.settings);
        return {{"saved", true}};
    }
    if (op == "exclude_review_app") {
        Config next = config_;
        next.settings.review_excluded_apps.push_back(r.at("app").get<std::string>());
        validate(next);
        save_config(next);
        config_ = std::move(next);
        return {{"saved", true}};
    }
    if (op == "mark") {
        const auto id = r.at("id").get<std::string>();
        std::optional<Event> event;
        for (const auto &e : recent_)
            if (e.id == id) {
                event = e;
                break;
            }
        if (!event)
            event = store_->find(id);
        if (!event)
            throw std::invalid_argument("Event no longer available");
        auto rule =
            make_rule(*event, r.at("verdict").get<std::string>(), r.value("scope", "connection"));
        Config next = config_;
        next.rules.erase(std::remove_if(next.rules.begin(), next.rules.end(),
                                        [&](const Rule &old) {
                                            return old.scope == rule.scope &&
                                                   lower_ascii(old.app) == lower_ascii(rule.app) &&
                                                   old.hash == rule.hash &&
                                                   old.remote_ip == rule.remote_ip &&
                                                   old.protocol == rule.protocol &&
                                                   old.remote_port == rule.remote_port;
                                        }),
                         next.rules.end());
        next.rules.push_back(rule);
        validate(next);
        save_config(next);
        config_ = std::move(next);
        return rule;
    }
    if (op == "delete_rule") {
        Config next = config_;
        const auto id = r.at("id").get<std::string>();
        next.rules.erase(std::remove_if(next.rules.begin(), next.rules.end(),
                                        [&](const Rule &rule) { return rule.id == id; }),
                         next.rules.end());
        save_config(next);
        config_ = std::move(next);
        return {{"saved", true}};
    }
    if (op == "import_rules") {
        if (!r.at("rules").is_array() || r.at("rules").size() > 5000)
            throw std::invalid_argument("Invalid rules array");
        for (const auto &item : r.at("rules")) {
            integer_bounds(item, "protocol", 255);
            integer_bounds(item, "remote_port", 65535);
            integer_bounds(item, "revision", UINT32_MAX);
            integer_bounds(item, "created", INT64_MAX);
            integer_bounds(item, "expires", INT64_MAX);
        }
        Config next = config_;
        next.rules = r.at("rules").get<std::vector<Rule>>();
        validate(next);
        save_config(next);
        config_ = std::move(next);
        return {{"saved", true}};
    }
    if (op == "feed") {
        Config next = config_;
        next.feed = r.at("feed").get<Feed>();
        validate(next);
        if (next.feed.expires <= now_ms())
            throw std::invalid_argument("Indicator feed is already expired");
        save_config(next);
        config_ = std::move(next);
        return {{"saved", true}};
    }
    throw std::invalid_argument("Unknown operation");
}
} // namespace nm
