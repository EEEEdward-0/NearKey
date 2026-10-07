#include "settings.h"
#include "lan.h"

#include <windows.h>
#include <cwchar>

std::wstring DataDirectory()
{
    wchar_t localAppData[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) return {};
    std::wstring directory = std::wstring(localAppData) + L"\\BluetoothUnlock";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory;
}

std::wstring SettingsPath()
{
    return DataDirectory() + L"\\settings.ini";
}

bool ParseBluetoothAddress(const wchar_t* text, uint64_t& address)
{
    if (text == nullptr || wcslen(text) != 12) return false;
    address = 0;
    for (const wchar_t* cursor = text; *cursor; ++cursor)
    {
        unsigned digit;
        if (*cursor >= L'0' && *cursor <= L'9') digit = *cursor - L'0';
        else if (*cursor >= L'A' && *cursor <= L'F') digit = *cursor - L'A' + 10;
        else if (*cursor >= L'a' && *cursor <= L'f') digit = *cursor - L'a' + 10;
        else return false;
        address = (address << 4) | digit;
    }
    return true;
}

std::wstring FormatBluetoothAddress(uint64_t address)
{
    wchar_t text[13];
    swprintf_s(text, L"%012llX", address);
    return text;
}

Settings LoadSettings()
{
    Settings settings;
    const std::wstring path = SettingsPath();
    settings.unlockThreshold = GetPrivateProfileIntW(L"Signal", L"UnlockThreshold", -65, path.c_str());
    settings.lockThreshold = GetPrivateProfileIntW(L"Signal", L"LockThreshold", -80, path.c_str());
    settings.lockDelaySeconds = GetPrivateProfileIntW(L"Signal", L"LockDelaySeconds", 60, path.c_str());
    settings.automaticLock = GetPrivateProfileIntW(L"Behavior", L"AutomaticLock", 0, path.c_str()) != 0;
    settings.automaticUnlock = GetPrivateProfileIntW(L"Behavior", L"AutomaticUnlock", 1, path.c_str()) != 0;
    settings.unlockKey = GetPrivateProfileIntW(L"Behavior", L"UnlockKey", 13, path.c_str());
    if (settings.unlockKey != 13 && (settings.unlockKey < 'A' || settings.unlockKey > 'Z'))
        settings.unlockKey = 13;
    settings.unlockMode = GetPrivateProfileIntW(L"Behavior", L"UnlockMode", 0, path.c_str());
    if (settings.unlockMode < 0 || settings.unlockMode > 2) settings.unlockMode = 0;
    const int lanCount = GetPrivateProfileIntW(L"LAN", L"Count", -1, path.c_str());
    // A missing Count identifies the original single-device configuration.
    for (int i = 0; i < (lanCount < 0 ? 1 : (lanCount > 2 ? 2 : lanCount)); ++i)
    {
        wchar_t ip[64] = {}, mac[64] = {}, ipKey[32], macKey[32];
        if (lanCount < 0) { wcscpy_s(ipKey, L"IPv4"); wcscpy_s(macKey, L"Mac"); }
        else { swprintf_s(ipKey, L"IPv4%d", i); swprintf_s(macKey, L"Mac%d", i); }
        GetPrivateProfileStringW(L"LAN", ipKey, L"", ip, 64, path.c_str());
        GetPrivateProfileStringW(L"LAN", macKey, L"", mac, 64, path.c_str());
        uint64_t address = 0;
        ParseBluetoothAddress(mac, address);
        if (lanCount >= 0 || *ip || *mac) settings.lanDevices.push_back({ip, address});
    }
    // Invalid counts or duplicate identities must not weaken the all-device requirement.
    if (lanCount > 2 || (settings.lanDevices.size() == 2 &&
        (settings.lanDevices[0].mac == settings.lanDevices[1].mac ||
         settings.lanDevices[0].ipv4 == settings.lanDevices[1].ipv4)))
        settings.lanDevices = {{L"", 0}};
    if (settings.unlockThreshold < -100 || settings.unlockThreshold > -20) settings.unlockThreshold = -65;
    if (settings.lockThreshold < -100 || settings.lockThreshold > settings.unlockThreshold)
        settings.lockThreshold = settings.unlockThreshold < -80 ? settings.unlockThreshold : -80;
    if (settings.lockDelaySeconds < 10 || settings.lockDelaySeconds > 600)
        settings.lockDelaySeconds = 60;
    const int count = GetPrivateProfileIntW(L"Devices", L"Count", 0, path.c_str());
    for (int i = 0; i < count && i < 8; ++i)
    {
        wchar_t key[32], addressText[64], name[128];
        swprintf_s(key, L"Address%d", i);
        GetPrivateProfileStringW(L"Devices", key, L"", addressText, 64, path.c_str());
        uint64_t address;
        if (!ParseBluetoothAddress(addressText, address)) continue;
        swprintf_s(key, L"Name%d", i);
        GetPrivateProfileStringW(L"Devices", key, L"Device", name, 128, path.c_str());
        settings.devices.push_back({address, name});
    }
    return settings;
}

bool SaveSettings(const Settings& settings)
{
    if (settings.devices.size() > 8 || settings.unlockThreshold < -100 ||
        settings.unlockThreshold > -20 || settings.lockThreshold < -100 ||
        settings.lockThreshold > settings.unlockThreshold ||
        settings.lockDelaySeconds < 10 || settings.lockDelaySeconds > 600 ||
        (settings.unlockKey != 13 && (settings.unlockKey < 'A' || settings.unlockKey > 'Z')))
        return false;
    if (settings.unlockMode < 0 || settings.unlockMode > 2) return false;
    if (settings.lanDevices.size() > 2 || (settings.unlockMode != 0 && settings.lanDevices.empty())) return false;
    for (const auto& device : settings.lanDevices)
        if (!IsValidLanIpv4(device.ipv4) || device.mac == 0) return false;
    if (settings.lanDevices.size() == 2 &&
        (settings.lanDevices[0].mac == settings.lanDevices[1].mac ||
         settings.lanDevices[0].ipv4 == settings.lanDevices[1].ipv4)) return false;
    const std::wstring path = SettingsPath();
    wchar_t number[32];
    auto writeNumber = [&](const wchar_t* section, const wchar_t* key, int value)
    {
        swprintf_s(number, L"%d", value);
        return WritePrivateProfileStringW(section, key, number, path.c_str()) != FALSE;
    };
    if (!writeNumber(L"Signal", L"UnlockThreshold", settings.unlockThreshold) ||
        !writeNumber(L"Signal", L"LockThreshold", settings.lockThreshold) ||
        !writeNumber(L"Signal", L"LockDelaySeconds", settings.lockDelaySeconds) ||
        !writeNumber(L"Behavior", L"AutomaticLock", settings.automaticLock) ||
        !writeNumber(L"Behavior", L"AutomaticUnlock", settings.automaticUnlock) ||
        !writeNumber(L"Behavior", L"UnlockKey", settings.unlockKey) ||
        !writeNumber(L"Behavior", L"UnlockMode", settings.unlockMode) ||
        !writeNumber(L"Devices", L"Count", static_cast<int>(settings.devices.size())))
        return false;
    if (!writeNumber(L"LAN", L"Count", static_cast<int>(settings.lanDevices.size()))) return false;
    for (size_t i = 0; i < 2; ++i)
    {
        wchar_t ipKey[32], macKey[32];
        swprintf_s(ipKey, L"IPv4%zu", i); swprintf_s(macKey, L"Mac%zu", i);
        const bool exists = i < settings.lanDevices.size();
        if (!WritePrivateProfileStringW(L"LAN", ipKey, exists ? settings.lanDevices[i].ipv4.c_str() : nullptr, path.c_str()) ||
            !WritePrivateProfileStringW(L"LAN", macKey, exists ? FormatBluetoothAddress(settings.lanDevices[i].mac).c_str() : nullptr, path.c_str())) return false;
    }
    WritePrivateProfileStringW(L"LAN", L"IPv4", nullptr, path.c_str());
    WritePrivateProfileStringW(L"LAN", L"Mac", nullptr, path.c_str());
    for (size_t i = 0; i < settings.devices.size(); ++i)
    {
        wchar_t key[32];
        swprintf_s(key, L"Address%zu", i);
        if (!WritePrivateProfileStringW(L"Devices", key,
            FormatBluetoothAddress(settings.devices[i].address).c_str(), path.c_str())) return false;
        swprintf_s(key, L"Name%zu", i);
        if (!WritePrivateProfileStringW(L"Devices", key,
            settings.devices[i].name.c_str(), path.c_str())) return false;
    }
    for (size_t i = settings.devices.size(); i < 8; ++i)
    {
        wchar_t key[32];
        swprintf_s(key, L"Address%zu", i);
        WritePrivateProfileStringW(L"Devices", key, nullptr, path.c_str());
        swprintf_s(key, L"Name%zu", i);
        WritePrivateProfileStringW(L"Devices", key, nullptr, path.c_str());
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    return true;
}
