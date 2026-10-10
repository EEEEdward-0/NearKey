#include <windows.h>
#include <dpapi.h>
#include <sddl.h>
#include <conio.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

namespace
{
    constexpr wchar_t kConfigKey[] = L"SOFTWARE\\BluetoothUnlockDemo";
    constexpr wchar_t kAdminOnlyDacl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";

    bool ReadPassword(wchar_t (&buffer)[256])
    {
        size_t length = 0;
        for (;;)
        {
            const wchar_t character = _getwch();
            if (character == L'\r')
            {
                buffer[length] = L'\0';
                std::wprintf(L"\n");
                return length > 0;
            }
            if (character == L'\b')
            {
                if (length)
                {
                    --length;
                    std::wprintf(L"\b \b");
                    std::fflush(stdout);
                }
            }
            else if (character >= L' ' && length < 255)
            {
                buffer[length++] = character;
                // _getwch never echoes input; a marker confirms that the key was received.
                std::wprintf(L"*");
                std::fflush(stdout);
            }
        }
    }

    bool CurrentSid(PWSTR& sidText)
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
        DWORD bytes = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        TOKEN_USER* user = static_cast<TOKEN_USER*>(LocalAlloc(LPTR, bytes));
        const bool ok = user != nullptr &&
            GetTokenInformation(token, TokenUser, user, bytes, &bytes) &&
            ConvertSidToStringSidW(user->User.Sid, &sidText);
        LocalFree(user);
        CloseHandle(token);
        return ok;
    }
}

int wmain()
{
    std::wprintf(L"Bluetooth Unlock Demo setup\n");
    std::wprintf(L"Enter this Windows account's password locally. Nothing is sent to chat.\n");
    std::wprintf(L"Each character appears as *. Press Enter after each entry.\n");
    wchar_t first[256] = {};
    wchar_t second[256] = {};
    std::wprintf(L"Password: ");
    const bool firstOk = ReadPassword(first);
    std::wprintf(L"Confirm:  ");
    const bool secondOk = ReadPassword(second);
    if (!firstOk || !secondOk || wcscmp(first, second) != 0)
    {
        SecureZeroMemory(first, sizeof(first));
        SecureZeroMemory(second, sizeof(second));
        std::fwprintf(stderr, L"Passwords do not match. Nothing was saved.\n");
        return 1;
    }
    SecureZeroMemory(second, sizeof(second));

    PWSTR sid = nullptr;
    if (!CurrentSid(sid))
    {
        SecureZeroMemory(first, sizeof(first));
        std::fwprintf(stderr, L"Could not read current user SID.\n");
        return 1;
    }

    DATA_BLOB plain = { static_cast<DWORD>((wcslen(first) + 1) * sizeof(wchar_t)),
                        reinterpret_cast<BYTE*>(first) };
    DATA_BLOB protectedData = {};
    const BOOL protectedOk = CryptProtectData(&plain, L"Bluetooth Unlock Demo",
        nullptr, nullptr, nullptr,
        CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_UI_FORBIDDEN, &protectedData);
    SecureZeroMemory(first, sizeof(first));
    if (!protectedOk)
    {
        LocalFree(sid);
        std::fwprintf(stderr, L"Windows could not protect the password.\n");
        return 1;
    }

    HKEY key = nullptr;
    DWORD disposition = 0;
    const LONG opened = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kConfigKey, 0,
        nullptr, 0, KEY_SET_VALUE | WRITE_DAC, nullptr, &key, &disposition);
    if (opened != ERROR_SUCCESS)
    {
        LocalFree(protectedData.pbData);
        LocalFree(sid);
        std::fwprintf(stderr, L"Run setup as Administrator. No password was saved.\n");
        return 1;
    }

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    BOOL aclOk = ConvertStringSecurityDescriptorToSecurityDescriptorW(
        kAdminOnlyDacl, SDDL_REVISION_1, &descriptor, nullptr);
    if (aclOk)
    {
        aclOk = RegSetKeySecurity(key, DACL_SECURITY_INFORMATION, descriptor) == ERROR_SUCCESS;
    }
    LocalFree(descriptor);
    LONG result = ERROR_ACCESS_DENIED;
    if (aclOk)
    {
        result = RegSetValueExW(key, L"UserSid", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(sid),
            static_cast<DWORD>((wcslen(sid) + 1) * sizeof(wchar_t)));
        if (result == ERROR_SUCCESS)
        {
            result = RegSetValueExW(key, L"Password", 0, REG_BINARY,
                protectedData.pbData, protectedData.cbData);
        }
    }
    RegCloseKey(key);
    LocalFree(protectedData.pbData);
    LocalFree(sid);
    if (!aclOk || result != ERROR_SUCCESS)
    {
        std::fwprintf(stderr, L"Could not save protected credentials.\n");
        return 1;
    }
    std::wprintf(L"Protected credentials saved for this Windows user.\n");
    return 0;
}
