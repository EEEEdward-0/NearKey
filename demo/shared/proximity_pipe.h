#pragma once

#include <windows.h>
#include <strsafe.h>

constexpr DWORD kProximityPacketMagic = 0x424C5545; // "BLUE"
constexpr DWORD kProximityPacketVersion = 2;

struct ProximityPacketV1
{
    DWORD magic;
    DWORD version;
    ULONGLONG lastSeenTick;
    LONG rssi;
};

struct ProximityPacket
{
    DWORD magic;
    DWORD version;
    ULONGLONG lastSeenTick;
    LONG rssi;
    DWORD unlockKey;
};

// Version 2 uses the trailing padding in the x64 version 1 packet for unlockKey.
// When reading a version 1 packet, ignore that field and use Enter.
static_assert(sizeof(ProximityPacketV1) == sizeof(ProximityPacket));

inline bool BuildProximityPipeName(PCWSTR userSid, PWSTR buffer, size_t count)
{
    return userSid != nullptr && wcslen(userSid) <= 128 &&
        SUCCEEDED(StringCchPrintfW(buffer, count,
                                   L"\\\\.\\pipe\\BluetoothUnlockDemo-%s", userSid));
}
