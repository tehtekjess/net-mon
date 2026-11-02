#pragma once
#include "core/ip.hpp"
#include <vector>
namespace nm {
// Snapshot of IPv4 unicast prefixes on operational adapters.
std::vector<Ipv4Subnet> adapter_ipv4_subnets();
} // namespace nm
