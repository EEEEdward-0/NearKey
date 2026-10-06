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
};

std::wstring DataDirectory();
std::wstring SettingsPath();
bool ParseBluetoothAddress(const wchar_t* text, uint64_t& address);
std::wstring FormatBluetoothAddress(uint64_t address);
Settings LoadSettings();
bool SaveSettings(const Settings& settings);
