#include <windows.h>
#include <credentialprovider.h>
#include <cstdio>
#include <cwchar>
#include <sddl.h>
#include <conio.h>
#include "../credential-provider/proximity.h"

static int CompareStoredPassword()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 2;
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    auto user = static_cast<TOKEN_USER*>(LocalAlloc(LPTR, bytes));
    PWSTR sid = nullptr;
    if (!user || !GetTokenInformation(token, TokenUser, user, bytes, &bytes) ||
        !ConvertSidToStringSidW(user->User.Sid, &sid))
    {
        LocalFree(user);
        CloseHandle(token);
        return 2;
    }
    PWSTR stored = nullptr;
    const HRESULT loaded = LoadDemoPassword(sid, &stored);
    LocalFree(sid);
    LocalFree(user);
    CloseHandle(token);
    if (FAILED(loaded)) return 2;
    wchar_t entered[256] = {};
    size_t length = 0;
    std::wprintf(L"Enter the same Windows password, then press Enter: ");
    for (;;)
    {
        const wchar_t ch = _getwch();
        if (ch == L'\r') break;
        if (ch == L'\b') { if (length) --length; }
        else if (ch >= L' ' && length < 255) entered[length++] = ch;
    }
    entered[length] = L'\0';
    const bool match = wcscmp(entered, stored) == 0;
    SecureZeroMemory(entered, sizeof(entered));
    SecureZeroMemory(stored, wcslen(stored) * sizeof(wchar_t));
    CoTaskMemFree(stored);
    std::printf("\nStored password comparison: %s\n", match ? "match" : "different");
    return match ? 0 : 12;
}

static int VerifyStoredPassword()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 2;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    auto user = static_cast<TOKEN_USER*>(LocalAlloc(LPTR, size));
    PWSTR sid = nullptr;
    if (!user || !GetTokenInformation(token, TokenUser, user, size, &size) ||
        !ConvertSidToStringSidW(user->User.Sid, &sid))
    {
        LocalFree(user);
        CloseHandle(token);
        return 2;
    }
    wchar_t name[256], domain[256];
    DWORD nameSize = ARRAYSIZE(name), domainSize = ARRAYSIZE(domain);
    SID_NAME_USE type;
    PWSTR password = nullptr;
    const bool ready = LookupAccountSidW(nullptr, user->User.Sid, name, &nameSize,
        domain, &domainSize, &type) && SUCCEEDED(LoadDemoPassword(sid, &password));
    HANDLE logonToken = nullptr;
    DWORD error = 0;
    const bool valid = ready && LogonUserW(name, domain, password,
        LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &logonToken);
    if (!valid) error = ready ? GetLastError() : ERROR_NOT_FOUND;
    if (logonToken) CloseHandle(logonToken);
    std::printf("Stored credential local authentication: %s (error=%lu)\n",
                valid ? "success" : "failed", error);

    // A Microsoft-linked Windows account may require the MicrosoftAccount domain.
    bool microsoftValid = false;
    DWORD microsoftError = ERROR_NOT_FOUND;
    HKEY accountKeys = nullptr;
    if (ready && RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\IdentityCRL\\UserExtendedProperties", 0,
        KEY_ENUMERATE_SUB_KEYS, &accountKeys) == ERROR_SUCCESS)
    {
        wchar_t account[256];
        DWORD accountSize = ARRAYSIZE(account);
        if (RegEnumKeyExW(accountKeys, 0, account, &accountSize, nullptr,
                          nullptr, nullptr, nullptr) == ERROR_SUCCESS)
        {
            microsoftValid = LogonUserW(account, L"MicrosoftAccount", password,
                LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &logonToken);
            microsoftError = microsoftValid ? 0 : GetLastError();
            if (logonToken) CloseHandle(logonToken);
        }
        RegCloseKey(accountKeys);
    }
    std::printf("Stored credential Microsoft authentication: %s (error=%lu)\n",
                microsoftValid ? "success" : "failed", microsoftError);
    if (password)
    {
        SecureZeroMemory(password, wcslen(password) * sizeof(wchar_t));
        CoTaskMemFree(password);
    }
    LocalFree(sid);
    LocalFree(user);
    CloseHandle(token);
    return valid || microsoftValid ? 0 : 11;
}

using DllGetClassObjectFunction = HRESULT (STDAPICALLTYPE *)(REFCLSID, REFIID, void **);

int wmain(int argc, wchar_t **argv)
{
    if (argc == 2 && wcscmp(argv[1], L"--verify-password") == 0)
        return VerifyStoredPassword();
    if (argc == 2 && wcscmp(argv[1], L"--compare-password") == 0)
        return CompareStoredPassword();
    if (argc != 2 && argc != 3) return 2;
    const HMODULE module = LoadLibraryW(argv[1]);
    if (module == nullptr) return 3;
    const auto entry = reinterpret_cast<DllGetClassObjectFunction>(
        GetProcAddress(module, "DllGetClassObject"));
    GUID id;
    if (entry == nullptr ||
        FAILED(CLSIDFromString(L"{c6830b85-4394-479b-9998-e919461b6081}", &id)))
    {
        FreeLibrary(module);
        return 4;
    }
    IClassFactory *factory = nullptr;
    HRESULT result = entry(id, IID_PPV_ARGS(&factory));
    ICredentialProvider *provider = nullptr;
    if (SUCCEEDED(result))
    {
        result = factory->CreateInstance(nullptr, IID_PPV_ARGS(&provider));
        factory->Release();
    }
    DWORD fields = 0;
    if (SUCCEEDED(result))
    {
        result = provider->SetUsageScenario(CPUS_UNLOCK_WORKSTATION, 0);
        if (SUCCEEDED(result)) result = provider->GetFieldDescriptorCount(&fields);
        provider->Release();
    }
    FreeLibrary(module);
    if (SUCCEEDED(result))
    {
        std::printf("Credential provider loaded; fields=%lu\n", fields);
        HANDLE token = nullptr;
        bool isNear = false;
        if (argc == 3)
        {
            isNear = IsIPhoneNearby(argv[2]);
            std::printf("Target user's iPhone state: %s\n",
                        isNear ? "near" : "old or missing");
        }
        else if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            DWORD size = 0;
            GetTokenInformation(token, TokenUser, nullptr, 0, &size);
            auto user = static_cast<TOKEN_USER *>(LocalAlloc(LPTR, size));
            PWSTR sid = nullptr;
            if (user && GetTokenInformation(token, TokenUser, user, size, &size) &&
                ConvertSidToStringSidW(user->User.Sid, &sid))
            {
                isNear = IsIPhoneNearby(sid);
                std::printf("Current user's iPhone state: %s\n",
                            isNear ? "near" : "old or missing");
            }
            LocalFree(sid);
            LocalFree(user);
            CloseHandle(token);
        }
        return isNear ? 0 : 10;
    }
    std::printf("Credential provider failed: 0x%08lx\n", result);
    return 1;
}
