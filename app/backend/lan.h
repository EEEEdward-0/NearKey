#pragma once

#include <cstdint>
#include <string>

// A matching ARP reply proves that this IPv4/MAC pair is on a local link now.
// It does not prove physical proximity or protect against address spoofing.
bool ProbeLanDevice(const std::wstring& ipv4, uint64_t expectedMac);
bool IsValidLanIpv4(const std::wstring& ipv4);
