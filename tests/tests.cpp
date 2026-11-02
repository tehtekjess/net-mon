#include "core/engine.hpp"
#include "core/ip.hpp"
#include "platform/geo.hpp"
#include "core/model.hpp"
#include "platform/windows.hpp"
#include "storage/store.hpp"
#include <functional>
#include <iostream>
#include <thread>
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
void rejects(const std::function<void()> &fn, const char *message) {
    try {
        fn();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error(message);
}
nm::Event event() {
    nm::Event e;
    e.id = nm::random_id();
    e.time = nm::now_ms();
    e.app = "C:\\Apps\\client.exe";
    e.hash = std::string(64, 'a');
    e.process_verified = true;
    e.remote_ip = "203.0.113.1";
    e.local_ip = "192.0.2.1";
    e.remote_port = 443;
    e.local_port = 50000;
    e.protocol = 6;
    return e;
}
void rules() {
    auto e = event();
    nm::Config c;
    nm::classify(e, c, true, nm::now_ms());
    check(!e.alert && e.classification == "unknown", "Balanced novelty should be quiet");
    c.settings.alert_mode = "ask";
    nm::classify(e, c, true, nm::now_ms());
    check(e.alert && nm::should_log(e, c.settings), "Ask mode must retain review events");
    nm::classify(e, c, false, nm::now_ms());
    check(e.alert, "Ask mode must still flag unreviewed repeated connections");
    check(nm::match_key(e).size() == 64, "Remembered identity must have bounded size");
    auto app_rule = nm::make_rule(e, "good", "app");
    auto another = e;
    another.remote_ip = "203.0.113.99";
    another.remote_port = 8443;
    check(nm::matches(app_rule, another, nm::now_ms()), "Application scope must cover endpoints");
    check(app_rule.remote_ip.empty() && app_rule.remote_port == 0,
          "Application rule must be canonical");
    auto good = nm::make_rule(e, "good", "connection");
    c.rules.push_back(good);
    e.pid = 900;
    e.local_port = 56789;
    nm::classify(e, c, true, nm::now_ms());
    check(!e.alert && e.classification == "good", "Rules must ignore PID/source port");
    e.remote_port = 8443;
    nm::classify(e, c, true, nm::now_ms());
    check(e.alert, "Trust must not cover another port");
    e.remote_port = 443;
    auto bad = nm::make_rule(e, "bad", "connection");
    c.rules.push_back(bad);
    c.settings.alert_mode = "threats";
    nm::classify(e, c, false, nm::now_ms());
    check(e.alert && e.classification == "bad", "Bad must override Good in all modes");
    c.rules.pop_back();
    c.feed.expires = nm::now_ms() + 60000;
    c.feed.indicators.push_back({e.remote_ip, "Test indicator", "test"});
    nm::classify(e, c, false, nm::now_ms());
    check(e.alert && e.classification == "threat", "Fresh threat must override Good");
    c.feed.expires = 1;
    nm::classify(e, c, false, nm::now_ms());
    check(!e.alert && e.classification == "good", "Expired indicators must not alert");
    c.settings.alert_mode = "balanced";
    e.hash = std::string(64, 'b');
    nm::classify(e, c, false, nm::now_ms());
    check(e.alert && e.classification == "suspicious", "Executable change must invalidate trust");
    e.hash.clear();
    e.process_verified = false;
    nm::classify(e, c, false, nm::now_ms());
    check(e.classification != "good", "Unknown process cannot inherit trust");
    rejects([&] { nm::make_rule(e, "good", "connection"); },
            "Missing hash must not create a Good app rule");
    c.rules.clear();
    e.app = "C:\\Users\\user\\AppData\\Local\\Temp\\odd.exe";
    e.signature = "unsigned";
    e.process_verified = true;
    nm::classify(e, c, true, nm::now_ms());
    check(e.alert, "Combined unsigned temporary executable signal");
    e.signature = "unknown";
    nm::classify(e, c, true, nm::now_ms());
    check(!e.alert, "Missing signature is not unsigned");
    c.settings.alert_mode = "threats";
    e.signature = "unsigned";
    nm::classify(e, c, true, nm::now_ms());
    check(!e.alert, "Threats-only excludes heuristics");
    c.settings.log_all = true;
    check(nm::should_log(e, c.settings), "Log all must retain ordinary activity");
    check(nm::normalize_ip("2001:0db8:0:0:0:0:0:1") == "2001:db8::1", "IPv6 canonicalization");
    check(nm::normalize_ip("fe80::1%12") == "fe80::1%12", "IPv6 scope");
    rejects([] { nm::normalize_ip("not-an-address"); }, "Invalid IP accepted");
    rejects(
        [] {
            nm::Settings s;
            s.history_mib = 0;
            nm::validate(s);
        },
        "Zero history budget accepted");
    rejects(
        [] {
            nm::Settings s;
            s.alert_mode = "typo";
            nm::validate(s);
        },
        "Unknown alert mode accepted");
}
void review_filters() {
    nm::Settings settings;
    auto e = event();
    e.remote_ip = "127.23.4.5";
    check(!nm::excluded_from_review(e, settings), "Loopback must remain visible when disabled");
    settings.review_hide_loopback = true;
    for (const auto *ip : {"127.0.0.1", "127.23.4.5", "::1", "::ffff:127.0.0.1"}) {
        e.remote_ip = ip;
        check(nm::excluded_from_review(e, settings), "Loopback variant was not excluded");
    }
    e.remote_ip = "192.168.0.1";
    check(!nm::excluded_from_review(e, settings), "Loopback option must not hide LAN destinations");
    settings.review_excluded_apps = {"C:/Apps/CLIENT.exe", "c:\\apps\\client.exe"};
    nm::validate(settings);
    check(settings.review_excluded_apps.size() == 1, "Review paths must normalize and deduplicate");
    e.hash.clear();
    e.process_verified = false;
    check(nm::excluded_from_review(e, settings), "Review exclusion must not depend on a hash");
    nm::Config config;
    config.settings = settings;
    config.rules.push_back(nm::make_rule(e, "bad", "app"));
    nm::classify(e, config, false, nm::now_ms());
    check(e.alert && e.classification == "bad",
          "Review exclusion must not silence explicit Bad alerts");
    settings.review_excluded_apps.clear();
    check(!nm::excluded_from_review(e, settings),
          "Removed review exclusion must restore visibility");
    rejects(
        [&] {
            settings.review_excluded_apps = {"client.exe"};
            nm::validate(settings);
        },
        "Relative review paths must be rejected");
    check(nm::address_kind("10.0.0.1") == "Private network", "Private address classification");
    check(nm::address_kind("fe80::1%12") == "Link-local", "Scoped IPv6 link-local classification");
    check(nm::address_kind("fc00::1") == "Private network", "IPv6 ULA classification");
    check(nm::address_kind("8.8.8.8").empty(), "Public IP must remain eligible for country lookup");
}
void local_networks() {
    nm::Settings settings;
    auto e = event();
    e.remote_ip = "10.1.2.3";
    check(!nm::excluded_from_review(e, settings), "Local network filter must be optional");
    settings.review_hide_local_network = true;
    for (const auto *ip : {"10.1.2.3",
                           "172.16.0.1",
                           "172.31.255.255",
                           "192.168.1.1",
                           "127.0.0.1",
                           "169.254.1.2",
                           "224.0.0.251",
                           "239.255.255.250",
                           "255.255.255.255",
                           "192.168.1.255",
                           "100.64.0.1",
                           "100.127.255.255",
                           "::1",
                           "fe80::1%7",
                           "fc00::1",
                           "fd12:3456::1",
                           "ff02::fb",
                           "fec0::1",
                           "::ffff:192.168.1.2",
                           "::ffff:255.255.255.255",
                           "::",
                           "0.0.0.0"}) {
        e.remote_ip = ip;
        check(nm::excluded_from_review(e, settings), "Local network category was not excluded");
    }
    for (const auto *ip :
         {"8.8.8.8", "172.15.255.255", "172.32.0.1", "100.63.255.255", "100.128.0.1",
          "223.255.255.255", "2606:4700:4700::1111", "2001:db8::1", "not-an-ip"}) {
        e.remote_ip = ip;
        check(!nm::excluded_from_review(e, settings), "Local filter hid an unrelated address");
    }
    const std::vector<nm::Ipv4Subnet> subnets = {{0xc633642aU, 26}};
    e.remote_ip = "198.51.100.63";
    check(nm::excluded_from_review(e, settings, subnets),
          "Directed subnet broadcast was not excluded");
    e.remote_ip = "::ffff:198.51.100.63";
    check(nm::excluded_from_review(e, settings, subnets),
          "Mapped directed broadcast was not excluded");
    e.remote_ip = "198.51.100.255";
    check(!nm::excluded_from_review(e, settings, subnets),
          "Public address ending in .255 was guessed to be broadcast");
    check(!nm::is_subnet_broadcast("198.51.100.43", {{0xc633642aU, 31}}),
          "/31 unicast must not be broadcast");
    check(!nm::is_subnet_broadcast("198.51.100.42", {{0xc633642aU, 32}}),
          "/32 unicast must not be broadcast");
    e.remote_ip = "192.168.1.1";
    nm::Config config;
    config.settings = settings;
    config.rules.push_back(nm::make_rule(e, "bad", "connection"));
    nm::classify(e, config, false, nm::now_ms());
    check(e.alert && nm::should_log(e, settings), "Local-network filtering must retain Bad alerts");
    const auto restored = nm::Json(settings).get<nm::Settings>();
    check(restored.review_hide_local_network, "Local filter did not serialize");
    check(!nm::Json::object().get<nm::Settings>().review_hide_local_network,
          "Legacy settings migration");
}
void geo(const std::filesystem::path &root) {
    std::filesystem::create_directories(root);
    const auto file = root / L"fixture.csv";
    nm::atomic_write(
        file, "8.8.8.0,8.8.8.255,US\n1.0.0.0,1.0.0.255,AU\n2001:4860::,2001:4860::ffff,US\n");
    nm::GeoDatabase db;
    db.load(file);
    check(db.lookup("8.8.8.0").find("US") != std::string::npos, "Country lower boundary");
    check(db.lookup("8.8.8.255").find("US") != std::string::npos, "Country upper boundary");
    check(db.lookup("::ffff:8.8.8.8").find("US") != std::string::npos,
          "Mapped IPv4 country lookup");
    check(db.lookup("2001:4860::123").find("US") != std::string::npos, "IPv6 country lookup");
    check(db.lookup("8.8.9.0") == "Unknown", "Lookup must reject gaps between country ranges");
    check(db.lookup("127.0.0.1") == "Localhost", "Localhost must not receive a country");
    check(db.lookup("192.168.1.1") == "Private network", "LAN must not receive a country");
    nm::atomic_write(file, "8.8.8.0,8.8.8.255,US\n8.8.8.1,8.8.9.0,AU\n");
    rejects([&] { db.load(file); }, "Overlapping country data must be rejected");
    check(db.lookup("8.8.8.8").find("US") != std::string::npos,
          "Failed load must preserve prior data");
    db.load(nm::executable_path().parent_path() / L"data" / L"geoip-country.csv");
    check(db.size() > 100000, "Bundled offline database must load");
    check(db.lookup("8.8.8.8").find("US") != std::string::npos, "Bundled country lookup");
}
void storage(const std::filesystem::path &root) {
    nm::Settings settings;
    settings.history_mib = 32;
    settings.log_all = true;
    {
        nm::Store store(root, settings);
        auto e = event();
        store.append(e);
        store.append(e);
        check(store.query("", 0, 100).size() == 1, "Duplicate source event stored twice");
        check(store.find(e.id).has_value(), "Saved event not found");
        check(store.query("203.0.113").size() == 1, "Endpoint search failed");
        check(store.query("%").empty(), "Search wildcard was not escaped");
        std::atomic<bool> cancelled{true};
        rejects([&] { store.query("", 0, 100, false, &cancelled); },
                "Cancelled history request still ran");
        cancelled = false;
        check(store.query("", 0, 100, false, &cancelled).size() == 1,
              "Cancelled read affected later history queries");
        std::vector<nm::Event> batch;
        for (int i = 0; i < 6000; ++i) {
            e.id = "rotation-" + std::to_string(i);
            e.reason = std::string(3000, 'x');
            e.time = nm::now_ms();
            batch.push_back(e);
            if (batch.size() == 100) {
                store.append_batch(batch);
                batch.clear();
            }
        }
        check(store.bytes() <= 32ULL * 1024 * 1024, "History exceeded cap");
        check(!store.find("rotation-0").has_value(), "Oldest segments not evicted");
        check(store.find("rotation-5999").has_value(), "Newest event lost in rotation");
        check(store.query("", 0, 50).size() == 50, "Paging limit failed");
        check(store.query("", 50, 50).size() == 50, "Paging offset failed");
    }
    {
        nm::Store reopened(root, settings);
        check(reopened.find("rotation-5999").has_value(), "History did not survive restart");
    }
}
void engine(const std::filesystem::path &root) {
    {
        nm::Engine engine(root);
        nm::Settings s;
        s.log_all = true;
        s.alert_mode = "ask";
        s.review_hide_local_network = true;
        engine.request({{"op", "settings"}, {"settings", s}});
        auto e = event();
        e.app = nm::utf8(nm::executable_path().wstring());
        e.pid = GetCurrentProcessId();
        e.hash.clear();
        e.process_verified = false;
        engine.enqueue(e, "");
        for (int i = 0;
             i < 100 && engine.request({{"op", "status"}}).at("processed").get<int>() == 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto live = engine.request({{"op", "query"}, {"view", "live"}});
        check(live.size() == 1, "Worker did not process event");
        engine.request(
            {{"op", "mark"}, {"id", e.id}, {"verdict", "good"}, {"scope", "connection"}});
        check(engine.request({{"op", "query"}, {"view", "review"}}).empty(),
              "Published review view did not reflect acknowledged Good decision");
        check(engine.request({{"op", "query"}, {"view", "history"}})[0].at("classification") ==
                  live[0].at("classification"),
              "Current review decision rewrote the historical observation");
        auto rule = engine.request(
            {{"op", "mark"}, {"id", e.id}, {"verdict", "bad"}, {"scope", "connection"}});
        check(rule.at("verdict") == "bad", "Mark Bad failed");
        engine.request({{"op", "exclude_review_app"}, {"app", e.app}});
        check(engine.request({{"op", "query"}, {"view", "review"}}).empty(),
              "Excluded app still appears in review");
        check(engine.request({{"op", "query"}, {"view", "live"}}).size() == 1,
              "Review filter removed recent activity");
        check(engine.request({{"op", "query"}, {"view", "history"}}).size() == 1,
              "Review filter removed saved history");
        check(!engine.request({{"op", "notifications"}}).empty(),
              "Review filter removed pending alerts");
        rejects(
            [&] {
                engine.request({{"op", "settings"}, {"settings", {{"alert_mode", "invalid"}}}});
            },
            "Invalid configuration accepted");
        rejects(
            [&] {
                engine.request(
                    {{"op", "settings"}, {"settings", {{"history_mib", 4294967546ULL}}}});
            },
            "Oversize setting must not wrap to a valid value");
        rejects(
            [&] {
                engine.request(
                    {{"op", "import_rules"}, {"rules", nm::Json::array({{{"protocol", 65542}}})}});
            },
            "Imported protocol must not wrap to TCP");
        check(engine.request({{"op", "config"}}).at("settings").at("alert_mode") == "ask",
              "Failed update modified config");
    }
    {
        nm::Engine engine(root);
        auto c = engine.request({{"op", "config"}});
        check(c.at("settings").at("review_hide_local_network") == true,
              "Local-network filter did not persist");
        check(c.at("settings").at("review_excluded_apps").size() == 1,
              "Review exclusions did not survive restart");
        check(c.at("rules").size() == 1 && c.at("settings").at("log_all") == true,
              "Rules/settings did not persist");
        engine.request({{"op", "delete_rule"}, {"id", c.at("rules")[0].at("id")}});
        check(engine.request({{"op", "config"}}).at("rules").empty(), "Reset failed");
    }
    auto e = event();
    e.pid = GetCurrentProcessId();
    e.app = nm::utf8(nm::executable_path().wstring());
    e.time = 1;
    e.hash.clear();
    e.process_verified = false;
    const auto own_path = nm::executable_path().wstring();
    wchar_t device[32768]{};
    check(QueryDosDeviceW(own_path.substr(0, 2).c_str(), device, 32768) != 0,
          "Test executable drive could not be resolved");
    const auto native_path = nm::lower_ascii(nm::utf8(device)) + nm::utf8(own_path.substr(2));
    check(nm::lower_ascii(nm::dos_path(native_path)) == nm::lower_ascii(nm::utf8(own_path)),
          "Lowercase WFP device paths must resolve to drive paths");
    nm::Enricher enrich;
    enrich.enrich(e);
    check(!e.process_verified, "Process lifetime check failed");
}
void event_driven(const std::filesystem::path &root) {
    nm::Engine engine(root);
    auto initial = engine.request({{"op", "status"}});
    const auto rev = initial.at("revision").get<std::uint64_t>();
    auto watcher = std::async(std::launch::async, [&] {
        return engine.request({{"op", "watch"}, {"after", rev}, {"timeout_ms", 1000}});
    });
    auto settings = engine.request({{"op", "config"}}).get<nm::Config>().settings;
    settings.review_hide_loopback = true;
    engine.request({{"op", "settings"}, {"settings", settings}});
    check(watcher.wait_for(std::chrono::milliseconds(1200)) == std::future_status::ready,
          "Watch did not complete after configuration change");
    check(watcher.get().at("revision").get<std::uint64_t>() > rev,
          "Watch revision did not advance");
    check(engine.request({{"op", "status"}}).at("config_revision").get<std::uint64_t>() >= 2,
          "Config revision did not advance");
    rejects(
        [&] {
            engine.request({{"op", "watch"}, {"timeout_ms", 60000}});
        },
        "Unbounded watch timeout accepted");
    rejects(
        [&] {
            engine.request({{"op", "watch"}, {"after", -1}});
        },
        "Negative watch cursor accepted");
    std::atomic<bool> started{false};
    auto writes = std::async(std::launch::async, [&] {
        started = true;
        for (unsigned i = 0; i < 30; ++i) {
            settings.cooldown_seconds = 300 + i;
            engine.request({{"op", "settings"}, {"settings", settings}});
        }
    });
    while (!started)
        std::this_thread::yield();
    double maximum = 0;
    unsigned reads = 0;
    do {
        const auto start = std::chrono::steady_clock::now();
        auto status = engine.request({{"op", "status"}});
        engine.request({{"op", "query"}, {"view", "review"}});
        maximum = std::max(maximum, std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - start)
                                        .count());
        ++reads;
        std::this_thread::yield();
    } while (writes.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready);
    writes.get();
    check(reads > 0 && maximum < 100, "Live reads stalled during configuration disk writes");
    check(engine.request({{"op", "config"}}).at("settings").at("cooldown_seconds") == 329,
          "Serialized changes were lost");
    const auto current = engine.request({{"op", "status"}}).at("revision").get<std::uint64_t>();
    auto stopping = std::async(std::launch::async, [&] {
        return engine.request({{"op", "watch"}, {"after", current}, {"timeout_ms", 1000}});
    });
    engine.stop();
    check(stopping.wait_for(std::chrono::milliseconds(1200)) == std::future_status::ready,
          "Shutdown left a change subscriber blocked");
    check(stopping.get().at("stopped") == true, "Subscriber was not informed of shutdown");
    rejects(
        [&] {
            engine.request({{"op", "settings"}, {"settings", settings}});
        },
        "Stopped engine accepted a write");
}
} // namespace
int main() {
    auto root = std::filesystem::temp_directory_path() / ("netmon-tests-" + nm::random_id());
    try {
        rules();
        review_filters();
        local_networks();
        geo(root / L"geo");
        storage(root / L"storage");
        engine(root / L"engine");
        event_driven(root / L"events");
        std::filesystem::remove_all(root);
        std::cout << "Passed " << checks << " checks.\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << "FAILED after " << checks << " checks: " << ex.what()
                  << "\nArtifacts: " << root << "\n";
        return 1;
    }
}
