#include "settings.h"

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
    settings.unlockMode = GetPrivateProfileIntW(L"Behavior", L"UnlockMode", 0, path.c_str());
    if (settings.unlockMode < 0 || settings.unlockMode > 2) settings.unlockMode = 0;
    wchar_t lanIp[64] = {}, lanMac[64] = {};
    GetPrivateProfileStringW(L"LAN", L"IPv4", L"", lanIp, 64, path.c_str());
    GetPrivateProfileStringW(L"LAN", L"Mac", L"", lanMac, 64, path.c_str());
    settings.lanIp = lanIp;
    ParseBluetoothAddress(lanMac, settings.lanMac);
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
        settings.lockDelaySeconds < 10 || settings.lockDelaySeconds > 600)
        return false;
    if (settings.unlockMode < 0 || settings.unlockMode > 2) return false;
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
        !writeNumber(L"Behavior", L"UnlockMode", settings.unlockMode) ||
        !writeNumber(L"Devices", L"Count", static_cast<int>(settings.devices.size())))
        return false;
    if (!WritePrivateProfileStringW(L"LAN", L"IPv4", settings.lanIp.c_str(), path.c_str()) ||
        !WritePrivateProfileStringW(L"LAN", L"Mac",
            settings.lanMac ? FormatBluetoothAddress(settings.lanMac).c_str() : L"", path.c_str()))
        return false;
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
