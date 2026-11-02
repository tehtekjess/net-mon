#include "platform/networks.hpp"
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdexcept>
namespace nm {
std::vector<Ipv4Subnet> adapter_ipv4_subnets() {
    ULONG bytes = 15 * 1024;
    std::vector<unsigned char> buffer(bytes);
    ULONG status = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && status == ERROR_BUFFER_OVERFLOW; ++attempt) {
        if (bytes > 1024 * 1024)
            throw std::runtime_error("Adapter snapshot exceeds limit");
        buffer.resize(bytes);
        status = GetAdaptersAddresses(
            AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()), &bytes);
    }
    if (status == ERROR_NO_DATA)
        return {};
    if (status != NO_ERROR)
        throw std::runtime_error("Cannot refresh IPv4 adapter prefixes");
    std::vector<Ipv4Subnet> result;
    for (auto *adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()); adapter;
         adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp)
            continue;
        for (auto *address = adapter->FirstUnicastAddress; address; address = address->Next) {
            if (!address->Address.lpSockaddr || address->Address.lpSockaddr->sa_family != AF_INET ||
                address->OnLinkPrefixLength == 0 || address->OnLinkPrefixLength >= 31)
                continue;
            const auto *v4 = reinterpret_cast<const sockaddr_in *>(address->Address.lpSockaddr);
            result.push_back({ntohl(v4->sin_addr.s_addr), address->OnLinkPrefixLength});
        }
    }
    return result;
}
} // namespace nm
