#pragma once
#include "platform/windows.hpp"
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <winevt.h>
namespace nm {
struct AuditHealth {
    bool subscribed = false;
    std::uint64_t errors = 0;
    std::string message;
};
std::uint32_t audit_policy();
void enable_audit(const std::filesystem::path &root);
void restore_audit(const std::filesystem::path &root);
class AuditCollector {
    HANDLE stop_;
    std::function<void(Event, std::string)> callback_;
    std::thread thread_;
    std::mutex mutex_;
    AuditHealth health_;
    std::string bookmark_;
    void run() noexcept;
    void error(const std::string &message);

  public:
    AuditCollector(HANDLE stop, std::string bookmark,
                   std::function<void(Event, std::string)> callback);
    ~AuditCollector();
    AuditHealth health();
};
// Used by collector and event-schema tests. Empty result means inbound activity.
std::optional<Event> decode_audit(EVT_HANDLE event, EVT_HANDLE context);
} // namespace nm
