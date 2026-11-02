#pragma once
#include "core/model.hpp"
#include <filesystem>
#include <unordered_map>
#include <windows.h>
namespace nm {
class Handle {
    HANDLE value_ = nullptr;

  public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&other) noexcept : value_(other.release()) {}
    Handle &operator=(Handle &&other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }
    HANDLE get() const { return value_; }
    HANDLE release() {
        auto old = value_;
        value_ = nullptr;
        return old;
    }
    void reset(HANDLE v = nullptr) {
        if (value_ && value_ != INVALID_HANDLE_VALUE)
            CloseHandle(value_);
        value_ = v;
    }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
};
std::string utf8(const std::wstring &s);
std::wstring wide(const std::string &s);
std::string win_error(DWORD error = GetLastError());
std::filesystem::path data_dir();
std::filesystem::path executable_path();
std::string current_sid();
std::string token_sid(HANDLE token);
bool is_admin();
std::string read_text(const std::filesystem::path &path, std::size_t limit = 5 * 1024 * 1024);
void atomic_write(const std::filesystem::path &path, const std::string &text);
std::string dos_path(const std::string &path);
std::string hash_file(const std::filesystem::path &path);
class Enricher {
    struct Cached {
        Millis checked = 0;
        std::uint64_t stamp = 0;
        std::string hash, signature, process_key;
    };
    std::unordered_map<std::string, Cached> cache_;

  public:
    void enrich(Event &event);
};
} // namespace nm
