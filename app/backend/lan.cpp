#include "lan.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace
{
    bool ReadLanMac(const std::wstring& ipv4, uint64_t& result)
    {
        if (!IsValidLanIpv4(ipv4)) return false;
        IN_ADDR address{};
        InetPtonW(AF_INET, ipv4.c_str(), &address);
        unsigned char mac[8] = {};
        ULONG length = sizeof(mac);
        if (SendARP(address.S_un.S_addr, 0, mac, &length) != NO_ERROR || length != 6)
            return false;
        result = 0;
        for (unsigned index = 0; index < 6; ++index)
            result = (result << 8) | mac[index];
        return result != 0;
    }

    std::vector<LanScanDevice> ScanLan(uint64_t expectedMac)
    {
        ULONG bytes = 16384;
        std::vector<BYTE> buffer(bytes);
        auto adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
        ULONG error = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST |
            GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &bytes);
        if (error == ERROR_BUFFER_OVERFLOW)
        {
            buffer.resize(bytes);
            adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
            error = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST |
                GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &bytes);
        }
        if (error != NO_ERROR) return {};
        std::vector<ULONG> targets;
        std::set<std::pair<ULONG, unsigned>> subnets;
        for (auto adapter = adapters; adapter; adapter = adapter->Next)
        {
            if (adapter->OperStatus != IfOperStatusUp ||
                (adapter->IfType != IF_TYPE_ETHERNET_CSMACD && adapter->IfType != IF_TYPE_IEEE80211)) continue;
            for (auto item = adapter->FirstUnicastAddress; item; item = item->Next)
            {
                if (item->Address.lpSockaddr->sa_family != AF_INET) continue;
                const unsigned prefix = item->OnLinkPrefixLength;
                // Limit manual discovery to local /22-/30 networks; never sweep huge or VPN ranges.
                if (prefix < 22 || prefix > 30) continue;
                const auto source = reinterpret_cast<sockaddr_in*>(item->Address.lpSockaddr)->sin_addr.S_un.S_addr;
                const ULONG host = ntohl(source);
                if ((host >> 24) == 127 || (host >> 16) == 0xA9FE) continue;
                const ULONG mask = 0xFFFFFFFFUL << (32 - prefix);
                const ULONG network = host & mask;
                if (!subnets.emplace(network, prefix).second) continue;
                const ULONG broadcast = network | ~mask;
                for (ULONG address = network + 1; address < broadcast; ++address)
                    if (address != host) targets.push_back(address);
            }
        }
        std::vector<LanScanDevice> results;
        std::atomic<size_t> next{0};
        std::atomic<bool> found{false};
        std::mutex resultMutex;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        std::vector<std::thread> workers;
        const size_t count = targets.size() < 32 ? targets.size() : 32;
        for (size_t worker = 0; worker < count; ++worker)
            workers.emplace_back([&]()
            {
                while (!found.load() && std::chrono::steady_clock::now() < deadline)
                {
                    const size_t index = next.fetch_add(1);
                    if (index >= targets.size()) break;
                    IN_ADDR address{};
                    address.S_un.S_addr = htonl(targets[index]);
                    wchar_t text[INET_ADDRSTRLEN] = {};
                    if (!InetNtopW(AF_INET, &address, text, ARRAYSIZE(text))) continue;
                    uint64_t actualMac = 0;
                    // Let Windows pick the route when Wi-Fi and Ethernet share a subnet.
                    if (ReadLanMac(text, actualMac) && (!expectedMac || actualMac == expectedMac))
                    {
                        std::lock_guard<std::mutex> lock(resultMutex);
                        if (!found.load())
                        {
                            results.push_back({text, actualMac});
                            if (expectedMac) found = true;
                        }
                    }
                }
            });
        for (auto& worker : workers) worker.join();
        return results;
    }
}

bool IsValidLanIpv4(const std::wstring& ipv4)
{
    IN_ADDR address{};
    return InetPtonW(AF_INET, ipv4.c_str(), &address) == 1 &&
        address.S_un.S_addr != INADDR_ANY && address.S_un.S_addr != INADDR_BROADCAST;
}

bool DiscoverLanDevice(uint64_t mac, std::wstring& ipv4)
{
    if (!mac) return false;
    const auto results = ScanLan(mac);
    if (results.empty()) return false;
    ipv4 = results[0].ipv4;
    return true;
}

bool ProbeLanDevice(const std::wstring& ipv4, uint64_t expectedMac)
{
    uint64_t actualMac = 0;
    return expectedMac != 0 && ReadLanMac(ipv4, actualMac) && actualMac == expectedMac;
}

bool ResolveLanDevice(std::wstring& ipv4, uint64_t& mac)
{
    uint64_t actualMac = 0;
    if (ReadLanMac(ipv4, actualMac) && (mac == 0 || actualMac == mac))
    {
        mac = actualMac;
        return true;
    }
    if (mac == 0) return false;
    PMIB_IPNET_TABLE2 table = nullptr;
    if (GetIpNetTable2(AF_INET, &table) != NO_ERROR) return DiscoverLanDevice(mac, ipv4);
    bool found = false;
    for (ULONG index = 0; index < table->NumEntries && !found; ++index)
    {
        const auto& row = table->Table[index];
        if (row.PhysicalAddressLength != 6) continue;
        uint64_t cachedMac = 0;
        for (unsigned byte = 0; byte < 6; ++byte)
            cachedMac = (cachedMac << 8) | row.PhysicalAddress[byte];
        if (cachedMac != mac) continue;
        wchar_t address[INET_ADDRSTRLEN] = {};
        if (!InetNtopW(AF_INET, &row.Address.Ipv4.sin_addr, address, ARRAYSIZE(address))) continue;
        // Cached neighbors can be stale; require a fresh response before filling the form.
        if (ProbeLanDevice(address, mac))
        {
            ipv4 = address;
            found = true;
        }
    }
    FreeMibTable(table);
    return found || DiscoverLanDevice(mac, ipv4);
}

std::vector<LanScanDevice> ScanLanDevices()
{
    return ScanLan(0);
}
