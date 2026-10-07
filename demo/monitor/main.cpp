#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <sddl.h>

#include <atomic>
#include <cstdio>
#include <cwchar>
#include <string>
#include <thread>

#include "../shared/proximity_pipe.h"

using namespace winrt::Windows::Devices::Bluetooth::Advertisement;

namespace
{
    std::atomic<bool> running{true};

    BOOL WINAPI OnConsoleEvent(DWORD)
    {
        running = false;
        return TRUE;
    }

    bool ParseAddress(const wchar_t* text, uint64_t& value)
    {
        if (text == nullptr || wcslen(text) != 12)
        {
            return false;
        }
        value = 0;
        for (const wchar_t* cursor = text; *cursor; ++cursor)
        {
            unsigned digit = 0;
            if (*cursor >= L'0' && *cursor <= L'9') digit = *cursor - L'0';
            else if (*cursor >= L'A' && *cursor <= L'F') digit = *cursor - L'A' + 10;
            else if (*cursor >= L'a' && *cursor <= L'f') digit = *cursor - L'a' + 10;
            else return false;
            value = (value << 4) | digit;
        }
        return true;
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

    void ServePipe(const std::wstring& pipeName,
                   const std::atomic<ULONGLONG>& lastSeen,
                   const std::atomic<LONG>& lastRssi)
    {
        bool firstInstance = true;
        while (running)
        {
            HANDLE pipe = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_OUTBOUND |
                (firstInstance ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                1, sizeof(ProximityPacketV1), 0, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) return;
            firstInstance = false;
            const bool connected = ConnectNamedPipe(pipe, nullptr) ||
                                   GetLastError() == ERROR_PIPE_CONNECTED;
            if (connected)
            {
                const ProximityPacketV1 packet = {
                    kProximityPacketMagic, 1,
                    lastSeen.load(), lastRssi.load()
                };
                DWORD written = 0;
                WriteFile(pipe, &packet, sizeof(packet), &written, nullptr);
                FlushFileBuffers(pipe);
                DisconnectNamedPipe(pipe);
            }
            CloseHandle(pipe);
        }
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2 || argc > 3)
    {
        std::fwprintf(stderr, L"Usage: BluetoothMonitor.exe <paired BLE address, 12 hex digits> [RSSI threshold, default -65]\n");
        return 2;
    }
    uint64_t target = 0;
    if (!ParseAddress(argv[1], target))
    {
        std::fwprintf(stderr, L"Invalid BLE address.\n");
        return 2;
    }
    const int threshold = argc == 3 ? _wtoi(argv[2]) : -65;
    if (threshold < -100 || threshold > -20)
    {
        std::fwprintf(stderr, L"RSSI threshold must be between -100 and -20 dBm.\n");
        return 2;
    }

    PWSTR sid = nullptr;
    if (!CurrentSid(sid))
    {
        std::fwprintf(stderr, L"Cannot identify the current Windows user.\n");
        return 1;
    }
    wchar_t pipeBuffer[256];
    const bool pipeNameOk = BuildProximityPipeName(sid, pipeBuffer, ARRAYSIZE(pipeBuffer));
    LocalFree(sid);
    if (!pipeNameOk) return 1;
    const std::wstring pipeName(pipeBuffer);
    const std::wstring mutexName = L"Local\\BluetoothUnlockDemo-" + pipeName.substr(wcslen(L"\\\\.\\pipe\\BluetoothUnlockDemo-"));
    HANDLE mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (mutex) CloseHandle(mutex);
        std::fwprintf(stderr, L"A monitor for this Windows user is already running.\n");
        return 1;
    }
    std::atomic<ULONGLONG> lastSeen{0};
    std::atomic<LONG> lastRssi{0};
    std::thread server(ServePipe, std::cref(pipeName),
                       std::cref(lastSeen), std::cref(lastRssi));
    SetConsoleCtrlHandler(OnConsoleEvent, TRUE);
    winrt::init_apartment();
    BluetoothLEAdvertisementWatcher watcher;
    watcher.ScanningMode(BluetoothLEScanningMode::Active);
    auto token = watcher.Received([&](auto const&, BluetoothLEAdvertisementReceivedEventArgs const& event)
    {
        if (event.BluetoothAddress() != target || event.RawSignalStrengthInDBm() < threshold)
        {
            return;
        }
        const ULONGLONG now = GetTickCount64();
        ULONGLONG prior = lastSeen.load();
        if (now - prior < 2000 || !lastSeen.compare_exchange_strong(prior, now))
        {
            return;
        }
        const LONG rssi = event.RawSignalStrengthInDBm();
        lastRssi = rssi;
        std::wprintf(L"iPhone signal accepted: %d dBm\n", rssi);
    });
    watcher.Start();
    std::wprintf(L"Scanning paired iPhone at threshold %d dBm. Press Ctrl+C to stop.\n", threshold);
    while (running)
    {
        Sleep(500);
    }
    watcher.Stop();
    watcher.Received(token);
    // Connect once to release the server thread from its blocking accept call.
    HANDLE wake = CreateFileW(pipeName.c_str(), GENERIC_READ, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
    server.join();
    CloseHandle(mutex);
    return 0;
}
