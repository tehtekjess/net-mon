#pragma once
#include "core/ip.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>
namespace nm {
using Json = nlohmann::json;
using Millis = std::int64_t;
Millis now_ms();
std::string lower_ascii(std::string value);
std::string normalize_ip(const std::string &value);
std::string random_id();
struct Event {
    std::string id, source = "WFP Security audit";
    Millis time = 0;
    std::uint32_t pid = 0;
    std::string app, process_key, user, hash, signature = "unknown", identity_status;
    std::string local_ip, remote_ip;
    std::uint16_t local_port = 0, remote_port = 0, protocol = 0;
    std::string state = "policy-permitted", classification = "unknown", reason, rule_id;
    std::uint32_t rule_revision = 0;
    bool alert = false, process_verified = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Event, id, source, time, pid, app, process_key,
                                                user, hash, signature, local_ip, remote_ip,
                                                local_port, remote_port, protocol, state,
                                                classification, reason, rule_id, rule_revision,
                                                alert, process_verified, identity_status)
struct Rule {
    std::string id, verdict = "good", scope = "connection", app, hash, remote_ip, reason;
    std::uint16_t protocol = 0, remote_port = 0;
    std::uint32_t revision = 1;
    Millis created = 0, expires = 0;
    bool enabled = true;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Rule, id, verdict, scope, app, hash, remote_ip,
                                                reason, protocol, remote_port, revision, created,
                                                expires, enabled)
struct Settings {
    std::string alert_mode = "balanced";
    bool log_all = false, review_hide_loopback = false, review_hide_local_network = false;
    std::vector<std::string> review_excluded_apps;
    std::uint32_t history_mib = 250, retention_days = 30, cooldown_seconds = 300;
    Millis snooze_until = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, alert_mode, log_all, history_mib,
                                                retention_days, cooldown_seconds, snooze_until,
                                                review_hide_loopback, review_excluded_apps,
                                                review_hide_local_network)
struct Indicator {
    std::string ip, reason, source;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Indicator, ip, reason, source)
struct Feed {
    Millis expires = 0;
    std::vector<Indicator> indicators;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Feed, expires, indicators)
struct Config {
    int version = 1;
    Settings settings;
    std::vector<Rule> rules;
    Feed feed;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Config, version, settings, rules, feed)
void validate(Settings &s);
bool excluded_from_review(const Event &event, const Settings &settings,
                          const std::vector<Ipv4Subnet> &subnets = {});
void validate(Rule &rule);
void validate(Feed &feed);
void validate(Config &c);
std::string match_key(const Event &event);
bool matches(const Rule &rule, const Event &event, Millis now);
void classify(Event &event, const Config &config, bool first_seen, Millis now);
bool should_log(const Event &event, const Settings &settings);
Rule make_rule(const Event &event, const std::string &verdict, const std::string &scope);
} // namespace nm
