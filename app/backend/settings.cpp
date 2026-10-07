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
    settings.configurationVersion = GetPrivateProfileIntW(L"Meta", L"Version", 0, path.c_str());
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
    // Older schemas use the same fields or the single-device LAN migration above.
    // A newer schema must not accidentally enable unlock with misinterpreted defaults.
    if (settings.configurationVersion < 0 || settings.configurationVersion > 2)
        settings.automaticUnlock = false;
    return settings;
}

bool SaveSettings(const Settings& settings)
{
    if (settings.configurationVersion < 0 || settings.configurationVersion > 2) return false;
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
    std::wstring text = L"[Meta]\r\nVersion=2\r\n[Signal]\r\n";
    auto number = [&](const wchar_t* key, int value)
    { text += std::wstring(key) + L"=" + std::to_wstring(value) + L"\r\n"; };
    number(L"UnlockThreshold", settings.unlockThreshold);
    number(L"LockThreshold", settings.lockThreshold);
    number(L"LockDelaySeconds", settings.lockDelaySeconds);
    text += L"[Behavior]\r\n";
    number(L"AutomaticLock", settings.automaticLock);
    number(L"AutomaticUnlock", settings.automaticUnlock);
    number(L"UnlockKey", settings.unlockKey);
    number(L"UnlockMode", settings.unlockMode);
    text += L"[LAN]\r\n";
    number(L"Count", static_cast<int>(settings.lanDevices.size()));
    for (size_t i = 0; i < settings.lanDevices.size(); ++i)
    {
        text += L"IPv4" + std::to_wstring(i) + L"=" + settings.lanDevices[i].ipv4 + L"\r\n";
        text += L"Mac" + std::to_wstring(i) + L"=" + FormatBluetoothAddress(settings.lanDevices[i].mac) + L"\r\n";
    }
    text += L"[Devices]\r\n";
    number(L"Count", static_cast<int>(settings.devices.size()));
    for (size_t i = 0; i < settings.devices.size(); ++i)
    {
        // INI values cannot contain line breaks or be interpreted as surrounding quotes.
        auto name = settings.devices[i].name;
        for (auto& c : name) if (c == L'\r' || c == L'\n' || c == L'"') c = L' ';
        text += L"Address" + std::to_wstring(i) + L"=" + FormatBluetoothAddress(settings.devices[i].address) + L"\r\n";
        text += L"Name" + std::to_wstring(i) + L"=" + name + L"\r\n";
    }
    const std::wstring path = SettingsPath();
    wchar_t temporary[MAX_PATH] = {};
    if (!GetTempFileNameW(DataDirectory().c_str(), L"cfg", 0, temporary)) return false;
    HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    bool saved = false;
    if (file != INVALID_HANDLE_VALUE)
    {
        // UTF-16 BOM preserves Chinese names for the Windows INI reader.
        const std::wstring contents = L"\xFEFF" + text;
        const DWORD bytes = static_cast<DWORD>(contents.size() * sizeof(wchar_t));
        DWORD written = 0;
        saved = WriteFile(file, contents.data(), bytes, &written, nullptr) && written == bytes && FlushFileBuffers(file);
        CloseHandle(file);
        // Same-directory replacement exposes a complete old or complete new file.
        if (saved) saved = MoveFileExW(temporary, path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!saved) DeleteFileW(temporary);
    return saved;
}
