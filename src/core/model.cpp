#include "core/model.hpp"
#include "core/ip.hpp"
#include <winsock2.h>
#include <windows.h>
#include <algorithm>
#include <bcrypt.h>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <ws2tcpip.h>
namespace nm {
Millis now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
std::string lower_ascii(std::string s) {
    for (auto &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string normalize_ip(const std::string &input) {
    if (input.empty() || input.size() > 64)
        throw std::invalid_argument("Invalid IP address");
    std::string value = input, scope;
    const auto pos = value.find('%');
    if (pos != std::string::npos) {
        scope = value.substr(pos);
        value.resize(pos);
        if (scope.size() < 2 || scope.size() > 11 ||
            !std::all_of(scope.begin() + 1, scope.end(),
                         [](char c) { return c >= '0' && c <= '9'; }))
            throw std::invalid_argument("Invalid IPv6 scope");
    }
    IN6_ADDR v6{};
    IN_ADDR v4{};
    char out[INET6_ADDRSTRLEN]{};
    if (scope.empty() && InetPtonA(AF_INET, value.c_str(), &v4) == 1) {
        InetNtopA(AF_INET, &v4, out, sizeof(out));
        return out;
    }
    if (InetPtonA(AF_INET6, value.c_str(), &v6) == 1) {
        InetNtopA(AF_INET6, &v6, out, sizeof(out));
        return lower_ascii(out) + scope;
    }
    throw std::invalid_argument("Invalid IP address: " + input);
}
std::string random_id() {
    unsigned char bytes[16]{};
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Unable to generate identifier");
    std::ostringstream out;
    for (auto c : bytes)
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c);
    return out.str();
}
void validate(Settings &s) {
    if (s.review_excluded_apps.size() > 128)
        throw std::invalid_argument("At most 128 review exclusions are allowed");
    std::vector<std::string> normalized;
    for (auto path : s.review_excluded_apps) {
        std::replace(path.begin(), path.end(), '/', '\\');
        path = lower_ascii(path);
        if (path.size() < 4 || path.size() > 32768 ||
            path.find_first_of("\r\n") != std::string::npos ||
            path.find('\0') != std::string::npos ||
            !((path[1] == ':' && path[2] == '\\' && path[0] >= 'a' && path[0] <= 'z') ||
              path.rfind("\\\\", 0) == 0))
            throw std::invalid_argument("Review exclusions require full executable paths");
        if (std::find(normalized.begin(), normalized.end(), path) == normalized.end())
            normalized.push_back(std::move(path));
    }
    s.review_excluded_apps = std::move(normalized);
    if (s.alert_mode != "balanced" && s.alert_mode != "ask" && s.alert_mode != "threats")
        throw std::invalid_argument("Unknown alert mode");
    if (s.history_mib < 32 || s.history_mib > 4096)
        throw std::invalid_argument("History limit must be 32-4096 MiB");
    if (s.retention_days < 1 || s.retention_days > 3650)
        throw std::invalid_argument("Retention must be 1-3650 days");
    if (s.cooldown_seconds < 5 || s.cooldown_seconds > 86400)
        throw std::invalid_argument("Cooldown must be 5-86400 seconds");
    if (s.snooze_until < 0 || s.snooze_until > now_ms() + 7LL * 86400000)
        throw std::invalid_argument("Invalid snooze time");
}
bool excluded_from_review(const Event &e, const Settings &s,
                          const std::vector<Ipv4Subnet> &subnets) {
    if (s.review_hide_local_network &&
        (is_local_network(e.remote_ip) || is_subnet_broadcast(e.remote_ip, subnets)))
        return true;
    if (s.review_hide_loopback && is_loopback(e.remote_ip))
        return true;
    auto path = lower_ascii(e.app);
    std::replace(path.begin(), path.end(), '/', '\\');
    return std::find(s.review_excluded_apps.begin(), s.review_excluded_apps.end(), path) !=
           s.review_excluded_apps.end();
}
void validate(Rule &r) {
    if (r.id.empty() || r.id.size() > 64 || r.reason.size() > 1024 || r.app.size() > 32768 ||
        r.hash.size() > 64 || r.created < 0 || r.expires < 0)
        throw std::invalid_argument("Invalid rule fields");
    if (r.verdict != "good" && r.verdict != "bad")
        throw std::invalid_argument("Rule must be Good or Bad");
    if (r.scope != "connection" && r.scope != "app" && r.scope != "destination")
        throw std::invalid_argument("Unknown rule scope");
    if (r.scope != "destination" && r.app.empty())
        throw std::invalid_argument("Application required");
    if (r.scope != "app")
        r.remote_ip = normalize_ip(r.remote_ip);
    if (r.protocol > 255)
        throw std::invalid_argument("Invalid protocol");
    r.hash = lower_ascii(r.hash);
    if (!r.hash.empty() &&
        (r.hash.size() != 64 || !std::all_of(r.hash.begin(), r.hash.end(), [](char c) {
             return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         })))
        throw std::invalid_argument("Invalid SHA-256");
    if (r.verdict == "good" && r.scope != "destination" && r.hash.empty())
        throw std::invalid_argument(
            "A verified executable hash is required to mark an application Good. Use Hide app from "
            "review to hide it without granting trust");
}
void validate(Feed &feed) {
    if (feed.indicators.size() > 10000 || feed.expires < 0)
        throw std::invalid_argument("Invalid indicator feed");
    for (auto &i : feed.indicators) {
        i.ip = normalize_ip(i.ip);
        if (i.reason.size() > 1024 || i.source.size() > 512 || i.source.empty())
            throw std::invalid_argument("Each indicator needs a bounded reason and source");
    }
}
void validate(Config &c) {
    if (c.version != 1 || c.rules.size() > 5000)
        throw std::invalid_argument("Unsupported configuration");
    validate(c.settings);
    std::unordered_map<std::string, bool> ids;
    for (auto &r : c.rules) {
        validate(r);
        if (!ids.emplace(r.id, true).second)
            throw std::invalid_argument("Duplicate rule ID");
    }
    validate(c.feed);
}
std::string match_key(const Event &e) {
    auto value =
        Json::array({lower_ascii(e.app), e.hash, e.remote_ip, e.protocol, e.remote_port}).dump();
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE handle = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Hash provider unavailable");
    unsigned char digest[32]{};
    auto result = BCryptCreateHash(algorithm, &handle, nullptr, 0, nullptr, 0, 0);
    if (result >= 0)
        result = BCryptHashData(handle, reinterpret_cast<PUCHAR>(value.data()),
                                static_cast<ULONG>(value.size()), 0);
    if (result >= 0)
        result = BCryptFinishHash(handle, digest, sizeof(digest), 0);
    if (handle)
        BCryptDestroyHash(handle);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (result < 0)
        throw std::runtime_error("Identity hash failed");
    std::ostringstream out;
    for (auto b : digest)
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(b);
    return out.str();
}
bool matches(const Rule &r, const Event &e, Millis time) {
    if (!r.enabled || (r.expires && r.expires <= time))
        return false;
    if (r.scope != "destination") {
        if (lower_ascii(r.app) != lower_ascii(e.app))
            return false;
        if (!r.hash.empty() && (e.hash != r.hash || !e.process_verified))
            return false;
    }
    return r.scope == "app" || (r.remote_ip == e.remote_ip && r.protocol == e.protocol &&
                                r.remote_port == e.remote_port);
}
void classify(Event &e, const Config &c, bool first, Millis time) {
    e.alert = false;
    e.rule_id.clear();
    e.rule_revision = 0;
    e.classification = "unknown";
    e.reason = first ? "First observed application/destination"
                     : "Previously observed; not explicitly trusted";
    const Rule *good = nullptr;
    for (const auto &r : c.rules) {
        if (!matches(r, e, time))
            continue;
        if (r.verdict == "bad") {
            e.classification = "bad";
            e.reason = "Matches your Bad rule";
            e.rule_id = r.id;
            e.rule_revision = r.revision;
            e.alert = true;
            return;
        }
        good = &r;
    }
    if (c.feed.expires > time)
        for (const auto &i : c.feed.indicators)
            if (i.ip == e.remote_ip) {
                e.classification = "threat";
                e.reason = "Threat indicator (" + i.source + "): " + i.reason;
                e.alert = true;
                return;
            }
    if (good) {
        e.classification = "good";
        e.reason = "Matches your Good rule";
        e.rule_id = good->id;
        e.rule_revision = good->revision;
        return;
    }
    const auto path = lower_ascii(e.app);
    if (c.settings.alert_mode != "threats") {
        for (const auto &r : c.rules) {
            if (r.verdict == "good" && r.enabled && (!r.expires || r.expires > time) &&
                r.scope != "destination" && lower_ascii(r.app) == path && !e.hash.empty() &&
                e.process_verified && !r.hash.empty() && r.hash != e.hash &&
                (r.scope == "app" || (r.remote_ip == e.remote_ip && r.protocol == e.protocol &&
                                      r.remote_port == e.remote_port))) {
                e.classification = "suspicious";
                e.reason = "Executable changed since you trusted this connection";
                e.alert = true;
                return;
            }
        }
        if (first && e.process_verified && e.signature == "unsigned" &&
            (path.find("\\temp\\") != std::string::npos ||
             path.find("\\tmp\\") != std::string::npos)) {
            e.classification = "suspicious";
            e.reason = "Unfamiliar unsigned executable connecting from a temporary directory";
            e.alert = true;
            return;
        }
        if (c.settings.alert_mode == "ask") {
            e.classification = "review";
            e.reason = "New application/destination needs your review";
            e.alert = true;
        }
    }
}
bool should_log(const Event &e, const Settings &s) { return s.log_all || e.alert; }
Rule make_rule(const Event &e, const std::string &verdict, const std::string &scope) {
    Rule r;
    r.id = random_id();
    r.created = now_ms();
    r.verdict = verdict;
    r.scope = scope;
    r.app = e.app;
    r.hash = e.process_verified ? e.hash : "";
    r.remote_ip = e.remote_ip;
    r.protocol = e.protocol;
    r.remote_port = e.remote_port;
    if (scope == "app") {
        r.remote_ip.clear();
        r.protocol = 0;
        r.remote_port = 0;
    }
    if (scope == "destination") {
        r.app.clear();
        r.hash.clear();
    }
    validate(r);
    return r;
}
} // namespace nm
