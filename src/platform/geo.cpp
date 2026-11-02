#include "platform/geo.hpp"
#include "platform/windows.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
namespace nm {
namespace {
thread_local std::unordered_map<std::string, std::string> *country_names = nullptr;
BOOL CALLBACK country_name(GEOID id) {
    wchar_t code[8]{}, name[256]{};
    if (GetGeoInfoW(id, GEO_ISO2, code, 8, 0) > 0 &&
        GetGeoInfoW(id, GEO_FRIENDLYNAME, name, 256, 0) > 0 && wcslen(code) == 2)
        country_names->emplace(utf8(code), utf8(name));
    return TRUE;
}
std::string field(std::string value) {
    if (!value.empty() && value.back() == '\r')
        value.pop_back();
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
    return value;
}
} // namespace
void GeoDatabase::load(const std::filesystem::path &csv) {
    if (std::filesystem::file_size(csv) > 96ULL * 1024 * 1024)
        throw std::runtime_error("Country database exceeds 96 MiB");
    std::ifstream input(csv, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open country database");
    std::vector<Range> v4, v6;
    std::string line;
    std::size_t count = 0;
    while (std::getline(input, line)) {
        if (line.empty())
            continue;
        if (line.size() > 160 || ++count > 1500000)
            throw std::runtime_error("Invalid country database size");
        const auto a = line.find(','), b = line.find(',', a == std::string::npos ? 0 : a + 1);
        if (a == std::string::npos || b == std::string::npos ||
            line.find(',', b + 1) != std::string::npos)
            throw std::runtime_error("Expected DB-IP country CSV with three fields");
        IpAddress first, last;
        auto code = field(line.substr(b + 1));
        if (!parse_ip(field(line.substr(0, a)), first, false) ||
            !parse_ip(field(line.substr(a + 1, b - a - 1)), last, false) || first.v6 != last.v6 ||
            last.bytes < first.bytes || code.size() != 2 ||
            !std::all_of(code.begin(), code.end(), [](char c) { return c >= 'A' && c <= 'Z'; }))
            throw std::runtime_error("Invalid country range");
        Range range{first.bytes, last.bytes, {code[0], code[1]}};
        (first.v6 ? v6 : v4).push_back(range);
    }
    if (input.bad() || count == 0)
        throw std::runtime_error("Country database is empty or unreadable");
    auto validate = [](auto &ranges) {
        std::sort(ranges.begin(), ranges.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
        for (std::size_t i = 1; i < ranges.size(); ++i)
            if (ranges[i].first <= ranges[i - 1].last)
                throw std::runtime_error("Overlapping country ranges");
        ranges.shrink_to_fit();
    };
    validate(v4);
    validate(v6);
    std::unordered_map<std::string, std::string> names;
    country_names = &names;
    EnumSystemGeoID(GEOCLASS_NATION, 0, country_name);
    country_names = nullptr;
    names["XK"] = "Kosovo";
    ipv4_ = std::move(v4);
    ipv6_ = std::move(v6);
    names_ = std::move(names);
}
CountryInfo GeoDatabase::lookup_country(const std::string &text) const {
    const auto kind = address_kind(text);
    if (!kind.empty())
        return {{}, kind};
    if (size() == 0)
        return {{}, "Database unavailable"};
    IpAddress address;
    if (!parse_ip(text, address))
        return {{}, "Invalid address"};
    const auto &ranges = address.v6 ? ipv6_ : ipv4_;
    auto it = std::upper_bound(ranges.begin(), ranges.end(), address.bytes,
                               [](const auto &value, const Range &r) { return value < r.first; });
    if (it == ranges.begin())
        return {{}, "Unknown"};
    --it;
    if (it->last < address.bytes)
        return {{}, "Unknown"};
    const std::string code(it->country, 2);
    if (code == "ZZ")
        return {{}, "Unknown"};
    const auto name = names_.find(code);
    return {code, name == names_.end() ? code : name->second + " (" + code + ")"};
}
} // namespace nm
