#pragma once
#include "core/model.hpp"
#include "platform/windows.hpp"
#include "storage/store.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_set>
namespace nm {
class Engine {
    struct Pending {
        Event event;
        std::string bookmark;
        std::size_t bytes;
        bool first = false;
    };
    struct Command {
        Json request;
        std::promise<Json> result;
        std::atomic<bool> cancelled{false};
    };
    struct Published {
        std::shared_ptr<const Config> config;
        std::deque<Event> recent;
        std::vector<Ipv4Subnet> subnets;
        std::string checkpoint;
        Json status;
        std::uint64_t revision = 0, config_revision = 0;
    };
    std::filesystem::path root_;
    Config config_;
    std::shared_ptr<const Config> published_config_;
    std::unique_ptr<Store> store_;
    Enricher enricher_;
    std::mutex queue_mutex_, watch_mutex_;
    std::condition_variable cv_, capture_cv_, changed_;
    std::deque<Pending> queue_, ready_;
    std::deque<std::shared_ptr<Command>> commands_;
    std::atomic<std::size_t> queue_bytes_{0};
    std::atomic<bool> stopped_{false};
    bool enrichment_done_ = false;
    std::thread worker_, enrichment_worker_;
    std::atomic<std::shared_ptr<const Published>> published_;
    std::unordered_map<std::string, Millis> seen_, cooldowns_;
    std::deque<Event> recent_, notifications_;
    std::unordered_set<std::string> recent_first_;
    std::string checkpoint_, storage_error_, network_error_;
    std::vector<Ipv4Subnet> adapter_subnets_;
    std::atomic<std::uint64_t> received_{0}, dropped_{0};
    std::uint64_t processed_ = 0, revision_ = 0, config_revision_ = 1;
    Millis started_ = now_ms(), last_save_ = 0;
    void run() noexcept;
    void enrich() noexcept;
    void publish();
    Json request_owned(const Json &request, std::atomic<bool> *cancelled);
    void save_config(const Config &config);
    void flush_state();

  public:
    explicit Engine(const std::filesystem::path &root);
    ~Engine();
    void enqueue(Event event, std::string bookmark);
    void stop();
    std::string checkpoint();
    Json request(const Json &request, const std::function<bool()> &connected = {});
};
} // namespace nm
