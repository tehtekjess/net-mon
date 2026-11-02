#include "platform/world.hpp"
#include "platform/geo.hpp"
#include "platform/windows.hpp"
#include <iostream>
#include <cmath>
int main() {
    int checks = 0;
    const auto check = [&](bool ok, const char *why) {
        ++checks;
        if (!ok)
            throw std::runtime_error(why);
    };
    try {
        auto world = std::make_shared<nm::WorldMap>();
        world->load(std::filesystem::path(__FILE__).parent_path().parent_path() / L"data" /
                    L"world-map.json");
        check(world->shapes().size() >= 230, "Incomplete bundled country data");
        for (const auto *code : {"US", "CA", "DE", "JP", "BR", "AU", "GB", "SG", "XK"})
            check(world->country(code) != nullptr, "Missing country label point");
        nm::Json records = nm::Json::array({{{"remote_ip", "8.8.8.8"},
                                             {"country_code", "US"},
                                             {"app", "C:\\Apps\\browser.exe"},
                                             {"remote_port", 443},
                                             {"protocol", 6},
                                             {"classification", "good"}},
                                            {{"remote_ip", "8.8.8.8"},
                                             {"country_code", "US"},
                                             {"app", "C:\\Apps\\updater.exe"},
                                             {"remote_port", 53},
                                             {"protocol", 17},
                                             {"classification", "bad"}},
                                            {{"remote_ip", "2606:4700:4700::1111"},
                                             {"country_code", "DE"},
                                             {"app", "C:\\Apps\\browser.exe"},
                                             {"remote_port", 443},
                                             {"protocol", 6},
                                             {"classification", "unknown"}},
                                            {{"remote_ip", "127.0.0.1"}, {"country_code", "US"}},
                                            {{"remote_ip", "192.168.1.1"}, {"country_code", ""}},
                                            {{"remote_ip", "::1"}, {"country_code", ""}},
                                            {{"remote_ip", "203.0.113.1"}, {"country_code", "US"}},
                                            {{"remote_ip", "9.9.9.9"}, {"country_code", "ZZ"}}});
        auto result = nm::prepare_map(records, world);
        check(result.groups.size() == 2 && result.mapped_ips == 2,
              "Country grouping did not deduplicate IPs");
        check(result.local_ips == 3 && result.unlocated_ips == 2, "Local or unlocated IP plotted");
        check(result.observations == 8, "Observation count changed during grouping");
        const auto us = std::find_if(result.groups.begin(), result.groups.end(),
                                     [](const auto &g) { return g.code == "US"; });
        check(us != result.groups.end() && us->endpoints.size() == 1 && us->severity == 3,
              "Worst classification did not survive grouping");
        check(us->endpoints[0].details.find(L"browser.exe") != std::wstring::npos &&
                  us->endpoints[0].details.find(L"updater.exe") != std::wstring::npos,
              "Shared destination lost process attribution");
        check(us->endpoints[0].subtitle.find(L"53/UDP") != std::wstring::npos &&
                  us->endpoints[0].subtitle.find(L"443/TCP") != std::wstring::npos,
              "Shared destination lost port/protocol attribution");
        check(us->location.longitude == world->country("US")->label.longitude,
              "Marker was not a country estimate");
        const auto missing = nm::prepare_map(records, {});
        check(missing.groups.empty() && missing.local_ips == 3 && missing.unlocated_ips == 4,
              "Missing map should not invent locations");
        check(nm::prepare_map(nm::Json::array(), world).groups.empty(),
              "Empty map contains markers");
        const auto again = nm::prepare_map(records, world);
        check(again.groups[0].code == result.groups[0].code, "Country order is unstable");
        nm::GeoDatabase geo;
        const auto local = geo.lookup_country("::1");
        check(local.code.empty() && !local.label.empty(), "Local lookup returned a country code");
        std::cout << nm::Json{{"passed", true},
                              {"checks", checks},
                              {"countries", world->shapes().size()}}
                         .dump(2)
                  << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Map test failed: " << e.what() << "\n";
        return 1;
    }
}
