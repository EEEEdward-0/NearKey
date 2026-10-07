#include "lan.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

bool IsValidLanIpv4(const std::wstring& ipv4)
{
    IN_ADDR address{};
    return InetPtonW(AF_INET, ipv4.c_str(), &address) == 1 &&
        address.S_un.S_addr != INADDR_ANY && address.S_un.S_addr != INADDR_BROADCAST;
}

bool ProbeLanDevice(const std::wstring& ipv4, uint64_t expectedMac)
{
    if (!IsValidLanIpv4(ipv4) || expectedMac == 0) return false;
    IN_ADDR address{};
    InetPtonW(AF_INET, ipv4.c_str(), &address);

    unsigned char mac[8] = {};
    ULONG length = sizeof(mac);
    if (SendARP(address.S_un.S_addr, 0, mac, &length) != NO_ERROR || length != 6)
        return false;
    for (unsigned index = 0; index < 6; ++index)
        if (mac[index] != static_cast<unsigned char>(expectedMac >> ((5 - index) * 8)))
            return false;
    return true;
}
