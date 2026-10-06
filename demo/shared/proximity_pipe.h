#pragma once

#include <windows.h>
#include <strsafe.h>

constexpr DWORD kProximityPacketMagic = 0x424C5545; // "BLUE"
constexpr DWORD kProximityPacketVersion = 1;

struct ProximityPacket
{
    DWORD magic;
    DWORD version;
    ULONGLONG lastSeenTick;
    LONG rssi;
};

inline bool BuildProximityPipeName(PCWSTR userSid, PWSTR buffer, size_t count)
{
    return userSid != nullptr && wcslen(userSid) <= 128 &&
        SUCCEEDED(StringCchPrintfW(buffer, count,
                                   L"\\\\.\\pipe\\BluetoothUnlockDemo-%s", userSid));
}
