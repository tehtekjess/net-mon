#pragma once
#include "core/model.hpp"
#include <atomic>
#include <filesystem>
#include <optional>
struct sqlite3;
namespace nm {
// Single-owner object: Engine serializes all access. No SQLite handles cross IPC.
class Store {
    std::filesystem::path dir_, active_;
    sqlite3 *db_ = nullptr;
    std::uint64_t sequence_ = 0;
    Settings settings_;
    void open_new();
    void close();
    std::vector<std::filesystem::path> segments() const;

  public:
    Store(const std::filesystem::path &root, Settings settings);
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;
    void append(const Event &e);
    void append_batch(const std::vector<Event> &events);
    void configure(const Settings &settings);
    void maintain();
    std::uint64_t bytes() const;
    Json query(std::string search, int offset = 0, int limit = 100, bool alerts_only = false,
               std::atomic<bool> *cancelled = nullptr);
    std::optional<Event> find(const std::string &id);
};
} // namespace nm
