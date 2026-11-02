#include "collectors/audit.hpp"
#include <ntsecapi.h>
#include <optional>
#include <stdexcept>
#include <vector>
namespace nm {
namespace {
const GUID connection_guid = {
    0x0cce9226, 0x69ae, 0x11d9, {0xbe, 0xd3, 0x50, 0x50, 0x54, 0x50, 0x30, 0x30}};
struct Evt {
    EVT_HANDLE p = nullptr;
    ~Evt() {
        if (p)
            EvtClose(p);
    }
};
void privilege() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &raw))
        throw std::runtime_error(win_error());
    Handle token(raw);
    TOKEN_PRIVILEGES p{};
    p.PrivilegeCount = 1;
    if (!LookupPrivilegeValueW(nullptr, SE_SECURITY_NAME, &p.Privileges[0].Luid))
        throw std::runtime_error(win_error());
    p.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    if (!AdjustTokenPrivileges(token.get(), FALSE, &p, 0, nullptr, nullptr) ||
        GetLastError() != ERROR_SUCCESS)
        throw std::runtime_error(win_error());
}
void set_policy(std::uint32_t flags) {
    privilege();
    AUDIT_POLICY_INFORMATION p{};
    p.AuditSubCategoryGuid = connection_guid;
    p.AuditingInformation = flags == 0 ? POLICY_AUDIT_EVENT_NONE : flags;
    if (!AuditSetSystemPolicy(&p, 1))
        throw std::runtime_error(win_error());
}
std::uint64_t numeric(const EVT_VARIANT &value) {
    switch (value.Type & EVT_VARIANT_TYPE_MASK) {
    case EvtVarTypeUInt16:
        return value.UInt16Val;
    case EvtVarTypeUInt32:
        return value.UInt32Val;
    case EvtVarTypeUInt64:
        return value.UInt64Val;
    case EvtVarTypeFileTime:
        return value.FileTimeVal;
    case EvtVarTypeString: {
        auto s = utf8(value.StringVal ? value.StringVal : L"");
        size_t end = 0;
        auto n = std::stoull(s, &end, s.rfind("0x", 0) == 0 ? 16 : 10);
        if (end != s.size())
            throw std::runtime_error("Malformed numeric audit field");
        return n;
    }
    default:
        throw std::runtime_error("Missing numeric audit field");
    }
}
std::string string_value(const EVT_VARIANT &value) {
    if ((value.Type & EVT_VARIANT_TYPE_MASK) != EvtVarTypeString || !value.StringVal)
        throw std::runtime_error("Missing audit text field");
    auto result = utf8(value.StringVal);
    if (result.size() > 32768)
        throw std::runtime_error("Oversize audit field");
    return result;
}
std::string bookmark_xml(EVT_HANDLE bookmark) {
    DWORD needed = 0, properties = 0;
    EvtRender(nullptr, bookmark, EvtRenderBookmark, 0, nullptr, &needed, &properties);
    if (needed > 65536)
        throw std::runtime_error("Oversize bookmark");
    std::vector<wchar_t> buffer(needed / sizeof(wchar_t) + 1);
    if (!EvtRender(nullptr, bookmark, EvtRenderBookmark, needed, buffer.data(), &needed,
                   &properties))
        throw std::runtime_error(win_error());
    return utf8(buffer.data());
}
} // namespace
std::uint32_t audit_policy() {
    privilege();
    PAUDIT_POLICY_INFORMATION p = nullptr;
    if (!AuditQuerySystemPolicy(&connection_guid, 1, &p))
        throw std::runtime_error(win_error());
    auto flags = p->AuditingInformation;
    AuditFree(p);
    return flags;
}
void enable_audit(const std::filesystem::path &root) {
    privilege();
    std::filesystem::create_directories(root);
    auto before = audit_policy();
    auto backup = root / L"audit-policy.json";
    const auto desired =
        static_cast<std::uint32_t>(POLICY_AUDIT_EVENT_SUCCESS | POLICY_AUDIT_EVENT_FAILURE);
    if (!std::filesystem::exists(backup))
        atomic_write(backup, Json{{"before", before}, {"written", desired}}.dump());
    set_policy(desired);
}
void restore_audit(const std::filesystem::path &root) {
    const auto path = root / L"audit-policy.json";
    if (!std::filesystem::exists(path))
        return;
    privilege();
    auto saved = Json::parse(read_text(path));
    if (audit_policy() == saved.at("written").get<std::uint32_t>())
        set_policy(saved.at("before").get<std::uint32_t>());
    std::filesystem::remove(path);
}
std::optional<Event> decode_audit(EVT_HANDLE event, EVT_HANDLE context) {
    DWORD bytes = 0, properties = 0;
    EvtRender(context, event, EvtRenderEventValues, 0, nullptr, &bytes, &properties);
    if (bytes == 0 || bytes > 128 * 1024)
        throw std::runtime_error("Invalid audit record size");
    std::vector<unsigned char> buffer(bytes);
    if (!EvtRender(context, event, EvtRenderEventValues, bytes, buffer.data(), &bytes, &properties))
        throw std::runtime_error(win_error());
    if (properties != 11)
        throw std::runtime_error("Unexpected audit schema");
    const auto *values = reinterpret_cast<const EVT_VARIANT *>(buffer.data());
    if (string_value(values[2]) != "%%14593")
        return {};
    Event e;
    auto pid = numeric(values[0]);
    if (pid > UINT32_MAX)
        throw std::runtime_error("Invalid PID");
    e.pid = static_cast<std::uint32_t>(pid);
    e.app = string_value(values[1]);
    e.local_ip = normalize_ip(string_value(values[3]));
    e.remote_ip = normalize_ip(string_value(values[4]));
    auto lp = numeric(values[5]), rp = numeric(values[6]), protocol = numeric(values[7]);
    if (lp > 65535 || rp > 65535 || protocol > 255)
        throw std::runtime_error("Invalid network fields");
    e.local_port = static_cast<std::uint16_t>(lp);
    e.remote_port = static_cast<std::uint16_t>(rp);
    e.protocol = static_cast<std::uint16_t>(protocol);
    const auto stamp = numeric(values[9]);
    e.time = static_cast<Millis>(stamp / 10000) - 11644473600000LL;
    e.id = "security:" + std::to_string(numeric(values[8])) + ":" + std::to_string(stamp);
    e.state = numeric(values[10]) == 5157 ? "policy-blocked" : "policy-permitted";
    return e;
}
AuditCollector::AuditCollector(HANDLE stop, std::string bookmark,
                               std::function<void(Event, std::string)> callback)
    : stop_(stop), callback_(std::move(callback)), thread_(), bookmark_(std::move(bookmark)) {
    thread_ = std::thread([this] { run(); });
}
AuditCollector::~AuditCollector() {
    SetEvent(stop_);
    if (thread_.joinable())
        thread_.join();
}
AuditHealth AuditCollector::health() {
    std::lock_guard<std::mutex> lock(mutex_);
    return health_;
}
void AuditCollector::error(const std::string &msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    health_.subscribed = false;
    ++health_.errors;
    health_.message = msg;
}
void AuditCollector::run() noexcept {
    while (WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) {
        try {
            // Start signaled so replay is drained before waiting for future records.
            Handle signal(CreateEventW(nullptr, TRUE, TRUE, nullptr));
            Evt bookmark;
            bookmark.p = EvtCreateBookmark(bookmark_.empty() ? nullptr : wide(bookmark_).c_str());
            if (!bookmark.p)
                throw std::runtime_error(win_error());
            Evt subscription;
            constexpr auto query = L"*[System[(EventID=5156 or EventID=5157)]]";
            DWORD flags = bookmark_.empty() ? EvtSubscribeToFutureEvents
                                            : (EvtSubscribeStartAfterBookmark | EvtSubscribeStrict);
            subscription.p =
                EvtSubscribe(nullptr, signal.get(), L"Security", query,
                             bookmark_.empty() ? nullptr : bookmark.p, nullptr, nullptr, flags);
            if (!subscription.p && !bookmark_.empty() &&
                GetLastError() == ERROR_EVT_QUERY_RESULT_STALE) {
                error("Security log bookmark expired; capture gap");
                bookmark_.clear();
                continue;
            }
            if (!subscription.p)
                throw std::runtime_error("EvtSubscribe: " + win_error());
            LPCWSTR paths[] = {L"Event/EventData/Data[@Name='ProcessID']",
                               L"Event/EventData/Data[@Name='Application']",
                               L"Event/EventData/Data[@Name='Direction']",
                               L"Event/EventData/Data[@Name='SourceAddress']",
                               L"Event/EventData/Data[@Name='DestAddress']",
                               L"Event/EventData/Data[@Name='SourcePort']",
                               L"Event/EventData/Data[@Name='DestPort']",
                               L"Event/EventData/Data[@Name='Protocol']",
                               L"Event/System/EventRecordID",
                               L"Event/System/TimeCreated/@SystemTime",
                               L"Event/System/EventID"};
            Evt context;
            context.p = EvtCreateRenderContext(11, paths, EvtRenderContextValues);
            if (!context.p)
                throw std::runtime_error("EvtCreateRenderContext: " + win_error());
            {
                std::lock_guard<std::mutex> lock(mutex_);
                health_.subscribed = true;
                health_.message = "Subscribed to WFP audit events";
            }
            HANDLE handles[] = {stop_, signal.get()};
            for (;;) {
                const auto wake = WaitForMultipleObjects(2, handles, FALSE, 1000);
                if (wake == WAIT_OBJECT_0)
                    break;
                if (wake != WAIT_OBJECT_0 + 1 && wake != WAIT_TIMEOUT)
                    throw std::runtime_error("Event wait: " + win_error());
                EVT_HANDLE batch[64]{};
                for (;;) {
                    DWORD returned = 0;
                    if (!EvtNext(subscription.p, 64, batch, 1000, 0, &returned)) {
                        const auto last = GetLastError();
                        // Windows 10 may report an empty subscription as INVALID_OPERATION.
                        // Only accept that result when no event handles were returned.
                        if (last == ERROR_NO_MORE_ITEMS || last == ERROR_TIMEOUT ||
                            (last == ERROR_INVALID_OPERATION && returned == 0))
                            break;
                        throw std::runtime_error("EvtNext: " + win_error(last));
                    }
                    for (DWORD i = 0; i < returned; ++i) {
                        Evt item;
                        item.p = batch[i];
                        try {
                            auto event = decode_audit(item.p, context.p);
                            if (!EvtUpdateBookmark(bookmark.p, item.p))
                                throw std::runtime_error(win_error());
                            bookmark_ = bookmark_xml(bookmark.p);
                            if (event)
                                callback_(std::move(*event), bookmark_);
                        } catch (const std::exception &ex) {
                            std::lock_guard<std::mutex> lock(mutex_);
                            ++health_.errors;
                            health_.message = ex.what();
                        }
                    }
                    if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0)
                        break;
                }
                ResetEvent(signal.get());
            }
        } catch (const std::exception &ex) {
            error(ex.what());
        } catch (...) {
            error("Unexpected collector failure");
        }
        if (WaitForSingleObject(stop_, 3000) == WAIT_OBJECT_0)
            break;
    }
}
} // namespace nm
