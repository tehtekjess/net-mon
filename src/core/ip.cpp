#include "core/ip.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
namespace nm {
bool parse_ip(const std::string &text, IpAddress &out, bool map_v4) {
    if (text.empty() || text.size() > 64 || text.find('\0') != std::string::npos)
        return false;
    auto value = text;
    auto scope = value.find('%');
    if (scope != std::string::npos) {
        if (scope + 1 == value.size() || !std::all_of(value.begin() + scope + 1, value.end(),
                                                      [](char c) { return c >= '0' && c <= '9'; }))
            return false;
        value.resize(scope);
    }
    out = {};
    if (scope == std::string::npos && InetPtonA(AF_INET, value.c_str(), out.bytes.data()) == 1)
        return true;
    if (InetPtonA(AF_INET6, value.c_str(), out.bytes.data()) != 1)
        return false;
    out.v6 = true;
    // Treat IPv4-mapped IPv6 addresses as their IPv4 destination.
    if (map_v4 &&
        std::all_of(out.bytes.begin(), out.bytes.begin() + 10, [](auto b) { return b == 0; }) &&
        out.bytes[10] == 255 && out.bytes[11] == 255) {
        IpAddress mapped{};
        std::copy(out.bytes.begin() + 12, out.bytes.end(), mapped.bytes.begin());
        out = mapped;
    }
    return true;
}
bool is_loopback(const std::string &text) {
    IpAddress ip;
    if (!parse_ip(text, ip))
        return false;
    return ip.v6
               ? std::all_of(ip.bytes.begin(), ip.bytes.end() - 1, [](auto b) { return b == 0; }) &&
                     ip.bytes[15] == 1
               : ip.bytes[0] == 127;
}
bool is_local_network(const std::string &text) {
    const auto kind = address_kind(text);
    return kind == "Localhost" || kind == "Private network" || kind == "Link-local" ||
           kind == "Multicast" || kind == "Broadcast" || kind == "Shared address space" ||
           kind == "Unspecified";
}
bool is_subnet_broadcast(const std::string &text, const std::vector<Ipv4Subnet> &subnets) {
    IpAddress ip;
    if (!parse_ip(text, ip) || ip.v6)
        return false;
    const auto &b = ip.bytes;
    const std::uint32_t address = (static_cast<std::uint32_t>(b[0]) << 24) |
                                  (static_cast<std::uint32_t>(b[1]) << 16) |
                                  (static_cast<std::uint32_t>(b[2]) << 8) | b[3];
    for (const auto &subnet : subnets) {
        if (subnet.prefix_length == 0 || subnet.prefix_length >= 31)
            continue;
        const auto mask = UINT32_MAX << (32 - subnet.prefix_length);
        if (address == ((subnet.address & mask) | ~mask))
            return true;
    }
    return false;
}
std::string address_kind(const std::string &text) {
    IpAddress ip;
    if (!parse_ip(text, ip))
        return "Invalid address";
    if (is_loopback(text))
        return "Localhost";
    const auto &b = ip.bytes;
    if (ip.v6) {
        if (std::all_of(b.begin(), b.end(), [](auto v) { return v == 0; }))
            return "Unspecified";
        if ((b[0] & 0xfe) == 0xfc)
            return "Private network";
        if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80)
            return "Link-local";
        if (b[0] == 0xff)
            return "Multicast";
        if (b[0] == 0xfe && (b[1] & 0xc0) == 0xc0)
            return "Private network";
        if (b[0] == 0x20 && b[1] == 1 && b[2] == 0x0d && b[3] == 0xb8)
            return "Documentation";
        if ((b[0] & 0xe0) != 0x20)
            return "Reserved / special";
    } else {
        if (b[0] == 255 && b[1] == 255 && b[2] == 255 && b[3] == 255)
            return "Broadcast";
        if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0)
            return "Unspecified";
        if (b[0] == 10 || (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 192 && b[1] == 168))
            return "Private network";
        if (b[0] == 169 && b[1] == 254)
            return "Link-local";
        if (b[0] == 100 && b[1] >= 64 && b[1] <= 127)
            return "Shared address space";
        if ((b[0] == 192 && b[1] == 0 && b[2] == 2) || (b[0] == 198 && b[1] == 51 && b[2] == 100) ||
            (b[0] == 203 && b[1] == 0 && b[2] == 113))
            return "Documentation";
        if (b[0] >= 224 && b[0] < 240)
            return "Multicast";
        if (b[0] == 0 || b[0] >= 240 || (b[0] == 198 && (b[1] == 18 || b[1] == 19)) ||
            (b[0] == 192 && b[1] == 0 && b[2] == 0))
            return "Reserved / special";
    }
    return {};
}
} // namespace nm
