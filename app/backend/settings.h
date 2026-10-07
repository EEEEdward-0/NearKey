#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SelectedDevice
{
    uint64_t address = 0;
    std::wstring name;
};

struct Settings
{
    std::vector<SelectedDevice> devices;
    int unlockThreshold = -65;
    int lockThreshold = -80;
    unsigned lockDelaySeconds = 60;
    bool automaticLock = false;
    bool automaticUnlock = true;
    // Enter or A-Z virtual-key code.
    int unlockKey = 13;
    // 0: Bluetooth, 1: LAN, 2: both.
    int unlockMode = 0;
    struct LanDevice
    {
        std::wstring ipv4;
        uint64_t mac = 0;
        bool operator==(const LanDevice& other) const { return ipv4 == other.ipv4 && mac == other.mac; }
    };
    std::vector<LanDevice> lanDevices;
};

std::wstring DataDirectory();
std::wstring SettingsPath();
bool ParseBluetoothAddress(const wchar_t* text, uint64_t& address);
std::wstring FormatBluetoothAddress(uint64_t address);
Settings LoadSettings();
bool SaveSettings(const Settings& settings);
