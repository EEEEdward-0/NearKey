#include "proximity.h"

#include <dpapi.h>
#include <strsafe.h>
#include <vector>
#include <sddl.h>
#include <wtsapi32.h>
#include <ntsecapi.h>
#include "../shared/proximity_pipe.h"

namespace
{
    constexpr wchar_t kConfigKey[] = L"SOFTWARE\\BluetoothUnlockDemo";
    constexpr ULONGLONG kMaxAgeMs = 45000;

    void RecordProbe(PCWSTR userSid, DWORD result, DWORD error, ULONGLONG age)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kConfigKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        {
            return;
        }
        RegSetValueExW(key, L"LastProbeResult", 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&result), sizeof(result));
        RegSetValueExW(key, L"LastProbeError", 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&error), sizeof(error));
        RegSetValueExW(key, L"LastProbeAgeMs", 0, REG_QWORD,
                       reinterpret_cast<const BYTE*>(&age), sizeof(age));
        if (userSid != nullptr && wcslen(userSid) <= 128)
        {
            RegSetValueExW(key, L"LastProbeSid", 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(userSid),
                           static_cast<DWORD>((wcslen(userSid) + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(key);
    }

    bool ReadString(HKEY root, PCWSTR path, PCWSTR name, std::vector<wchar_t>& value)
    {
        DWORD bytes = 0;
        if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
            bytes < sizeof(wchar_t) || bytes > 512)
        {
            return false;
        }
        value.resize(bytes / sizeof(wchar_t));
        return RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) == ERROR_SUCCESS;
    }
}

void RecordLoginFlow(PCWSTR valueName, DWORD value)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\BluetoothUnlockDemo", 0,
                      KEY_SET_VALUE, &key) == ERROR_SUCCESS)
    {
        RegSetValueExW(key, valueName, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
    }
}

bool IsExistingSessionForUser(PCWSTR userSid)
{
    if (userSid == nullptr) return false;
    PSID expectedSid = nullptr;
    if (!ConvertStringSidToSidW(userSid, &expectedSid)) return false;
    bool matches = false;
    ULONG count = 0;
    PLUID sessions = nullptr;
    const DWORD consoleSession = WTSGetActiveConsoleSessionId();
    if (LsaEnumerateLogonSessions(&count, &sessions) >= 0)
    {
        for (ULONG i = 0; i < count && !matches; ++i)
        {
            PSECURITY_LOGON_SESSION_DATA data = nullptr;
            if (LsaGetLogonSessionData(&sessions[i], &data) >= 0 && data != nullptr)
            {
                const bool interactive = data->LogonType == Interactive ||
                    data->LogonType == RemoteInteractive ||
                    data->LogonType == CachedInteractive;
                matches = data->Sid != nullptr && data->Session == consoleSession &&
                    interactive && EqualSid(data->Sid, expectedSid);
                LsaFreeReturnBuffer(data);
            }
        }
        LsaFreeReturnBuffer(sessions);
    }
    RecordLoginFlow(L"LastSessionScanCount", count);
    RecordLoginFlow(L"LastConsoleSession", consoleSession);
    LocalFree(expectedSid);
    return matches;
}

bool IsUnlockConditionMet(PCWSTR userSid)
{
    wchar_t pipeName[256];
    if (!BuildProximityPipeName(userSid, pipeName, ARRAYSIZE(pipeName)))
    {
        RecordProbe(userSid, 1, 0, 0);
        return false;
    }
    if (!WaitNamedPipeW(pipeName, 1000))
    {
        RecordProbe(userSid, 2, GetLastError(), 0);
        return false;
    }
    HANDLE pipe = CreateFileW(pipeName, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        RecordProbe(userSid, 2, GetLastError(), 0);
        return false;
    }
    DWORD serverPid = 0;
    HANDLE process = nullptr;
    HANDLE token = nullptr;
    PSID expectedSid = nullptr;
    bool trusted = false;
    if (GetNamedPipeServerProcessId(pipe, &serverPid) &&
        (process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, serverPid)) != nullptr &&
        OpenProcessToken(process, TOKEN_QUERY, &token) &&
        ConvertStringSidToSidW(userSid, &expectedSid))
    {
        DWORD bytes = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        std::vector<BYTE> tokenData(bytes);
        trusted = bytes != 0 &&
            GetTokenInformation(token, TokenUser, tokenData.data(), bytes, &bytes) &&
            EqualSid(reinterpret_cast<TOKEN_USER*>(tokenData.data())->User.Sid, expectedSid);
    }
    LocalFree(expectedSid);
    if (token) CloseHandle(token);
    if (process) CloseHandle(process);
    if (!trusted)
    {
        CloseHandle(pipe);
        RecordProbe(userSid, 5, GetLastError(), 0);
        return false;
    }
    ProximityPacket packet{};
    DWORD bytes = 0;
    const bool read = ReadFile(pipe, &packet, sizeof(packet), &bytes, nullptr) &&
                      bytes == sizeof(packet);
    CloseHandle(pipe);
    if (!read || packet.magic != kProximityPacketMagic ||
        packet.version != kProximityPacketVersion || packet.lastSeenTick == 0)
    {
        RecordProbe(userSid, 2, read ? ERROR_INVALID_DATA : GetLastError(), 0);
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG age = packet.lastSeenTick <= now ? now - packet.lastSeenTick : ~0ULL;
    const bool isNear = age <= kMaxAgeMs && packet.rssi >= -100 && packet.rssi <= -20;
    RecordProbe(userSid, isNear ? 4 : 3, 0, age);
    return isNear;
}

HRESULT LoadDemoPassword(PCWSTR userSid, PWSTR *password)
{
    if (password == nullptr)
    {
        return E_POINTER;
    }
    *password = nullptr;
    if (userSid == nullptr)
    {
        return E_INVALIDARG;
    }

    std::vector<wchar_t> configuredSid;
    if (!ReadString(HKEY_LOCAL_MACHINE, kConfigKey, L"UserSid", configuredSid) ||
        wcscmp(configuredSid.data(), userSid) != 0)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    DWORD bytes = 0;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kConfigKey, L"Password",
                     RRF_RT_REG_BINARY, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes == 0 || bytes > 8192)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    std::vector<BYTE> encrypted(bytes);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kConfigKey, L"Password",
                     RRF_RT_REG_BINARY, nullptr, encrypted.data(), &bytes) != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    DATA_BLOB input = { bytes, encrypted.data() };
    DATA_BLOB output = {};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    HRESULT result = E_INVALIDARG;
    if (output.cbData >= sizeof(wchar_t) && output.cbData <= 1024 &&
        output.cbData % sizeof(wchar_t) == 0 &&
        reinterpret_cast<wchar_t *>(output.pbData)[output.cbData / sizeof(wchar_t) - 1] == L'\0')
    {
        *password = static_cast<PWSTR>(CoTaskMemAlloc(output.cbData));
        if (*password != nullptr)
        {
            CopyMemory(*password, output.pbData, output.cbData);
            result = S_OK;
        }
        else
        {
            result = E_OUTOFMEMORY;
        }
    }
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}
