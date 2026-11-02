#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace nm {
struct IpAddress {
    std::array<std::uint8_t, 16> bytes{};
    bool v6 = false;
};
bool parse_ip(const std::string &text, IpAddress &result, bool map_v4 = true);
bool is_loopback(const std::string &text);
struct Ipv4Subnet {
    std::uint32_t address = 0;
    std::uint8_t prefix_length = 0;
};
bool is_local_network(const std::string &text);
bool is_subnet_broadcast(const std::string &text, const std::vector<Ipv4Subnet> &subnets);
// Empty means a public address. Local/reserved addresses are never geolocated.
std::string address_kind(const std::string &text);
} // namespace nm
