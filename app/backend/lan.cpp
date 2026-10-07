#include "lan.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

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
}

bool IsValidLanIpv4(const std::wstring& ipv4)
{
    IN_ADDR address{};
    return InetPtonW(AF_INET, ipv4.c_str(), &address) == 1 &&
        address.S_un.S_addr != INADDR_ANY && address.S_un.S_addr != INADDR_BROADCAST;
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
    if (GetIpNetTable2(AF_INET, &table) != NO_ERROR) return false;
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
    return found;
}
