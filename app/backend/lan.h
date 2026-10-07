#pragma once

#include <cstdint>
#include <string>

// A matching ARP reply proves that this IPv4/MAC pair is on a local link now.
// It does not prove physical proximity or protect against address spoofing.
bool ProbeLanDevice(const std::wstring& ipv4, uint64_t expectedMac);
bool IsValidLanIpv4(const std::wstring& ipv4);

// Resolve a known Wi-Fi MAC via cache, then bounded on-link ARP discovery;
// with only IPv4 supplied, read its MAC directly.
bool ResolveLanDevice(std::wstring& ipv4, uint64_t& mac);
bool DiscoverLanDevice(uint64_t mac, std::wstring& ipv4);

#include <vector>
struct LanScanDevice { std::wstring ipv4; uint64_t mac; };
std::vector<LanScanDevice> ScanLanDevices();
