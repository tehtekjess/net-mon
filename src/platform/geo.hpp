#pragma once
#include "core/ip.hpp"
#include <filesystem>
#include <unordered_map>
#include <vector>
namespace nm {
struct CountryInfo {
    std::string code, label;
};
class GeoDatabase {
    struct Range {
        std::array<std::uint8_t, 16> first{}, last{};
        char country[2]{};
    };
    std::vector<Range> ipv4_, ipv6_;
    std::unordered_map<std::string, std::string> names_;

  public:
    // Load once on the UI's background worker; never on the capture thread.
    void load(const std::filesystem::path &csv);
    CountryInfo lookup_country(const std::string &ip) const;
    std::string lookup(const std::string &ip) const { return lookup_country(ip).label; }
    std::size_t size() const { return ipv4_.size() + ipv6_.size(); }
};
} // namespace nm
