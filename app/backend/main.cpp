#include <windows.h>
#include <sddl.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Enumeration.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "decision.h"
#include "lan.h"
#include "settings.h"
#include "../../demo/shared/proximity_pipe.h"

using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Enumeration;

namespace
{
    std::atomic<bool> running{true};

    BOOL WINAPI StopOnConsoleEvent(DWORD)
    {
        running = false;
        return TRUE;
    }

    bool CurrentSid(PWSTR& text)
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
        DWORD bytes = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        std::vector<BYTE> buffer(bytes);
        const bool success = bytes && GetTokenInformation(token, TokenUser,
            buffer.data(), bytes, &bytes) &&
            ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &text);
        CloseHandle(token);
        return success;
    }

    std::string Utf8(const std::wstring& value)
    {
        if (value.empty()) return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string converted(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
            converted.data(), length, nullptr, nullptr);
        return converted;
    }

    std::wstring CleanName(std::wstring value)
    {
        for (wchar_t& ch : value)
            if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
        if (value.size() > 80) value.resize(80);
        return value;
    }

    bool WriteText(const std::wstring& path, const std::string& content)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        const bool okay = WriteFile(file, content.data(), static_cast<DWORD>(content.size()),
            &written, nullptr) && written == content.size();
        CloseHandle(file);
        return okay;
    }

    void LogEvent(const char* event)
    {
        SYSTEMTIME time;
        GetLocalTime(&time);
        char line[128];
        sprintf_s(line, "%04u-%02u-%02u %02u:%02u:%02u\t%s\r\n",
            time.wYear, time.wMonth, time.wDay, time.wHour,
            time.wMinute, time.wSecond, event);
        const std::wstring path = DataDirectory() + L"\\events.log";
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) &&
            attributes.nFileSizeLow > 1024 * 1024 && attributes.nFileSizeHigh == 0)
        {
            const std::wstring previous = DataDirectory() + L"\\events.previous.log";
            MoveFileExW(path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        CloseHandle(file);
    }

    struct SeenDevice
    {
        int rssi;
        ULONGLONG tick;
        std::wstring name;
    };

    std::map<uint64_t, std::wstring> PairedBleDevices()
    {
        // Pairing supplies names for exact address matches; it is not proof of proximity.
        std::map<uint64_t, std::wstring> paired;
        const auto devices = DeviceInformation::FindAllAsync(
            BluetoothLEDevice::GetDeviceSelectorFromPairingState(true)).get();
        for (const auto& device : devices)
        {
            try
            {
                const auto bluetooth = BluetoothLEDevice::FromIdAsync(device.Id()).get();
                if (bluetooth && bluetooth.BluetoothAddress() != 0)
                    paired[bluetooth.BluetoothAddress()] = CleanName(device.Name().c_str());
            }
            catch (const winrt::hresult_error&) {}
        }
        return paired;
    }

    void ServeProximity(const std::wstring& pipeName,
                        const std::atomic<ULONGLONG>& lastEligible,
                        const std::atomic<LONG>& meanRssi,
                        const std::atomic<DWORD>& unlockKey)
    {
        bool first = true;
        while (running)
        {
            HANDLE pipe = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_OUTBOUND |
                (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                PIPE_REJECT_REMOTE_CLIENTS, 1, sizeof(ProximityPacket), 0, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) return;
            first = false;
            if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED)
            {
                const ProximityPacket packet = {
                    kProximityPacketMagic, kProximityPacketVersion,
                    lastEligible.load(), meanRssi.load(), unlockKey.load()
                };
                DWORD written = 0;
                WriteFile(pipe, &packet, sizeof(packet), &written, nullptr);
                FlushFileBuffers(pipe);
                DisconnectNamedPipe(pipe);
            }
            CloseHandle(pipe);
        }
    }

    int RunMonitor()
    {
        PWSTR sid = nullptr;
        if (!CurrentSid(sid)) return 1;
        wchar_t pipeName[256];
        if (!BuildProximityPipeName(sid, pipeName, ARRAYSIZE(pipeName)))
        {
            LocalFree(sid);
            return 1;
        }
        const std::wstring mutexName = std::wstring(L"Local\\BluetoothUnlockDemo-") + sid;
        LocalFree(sid);
        HANDLE singleton = CreateMutexW(nullptr, TRUE, mutexName.c_str());
        if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS)
        {
            if (singleton) CloseHandle(singleton);
            return 2;
        }

        std::atomic<ULONGLONG> lastEligible{0};
        std::atomic<LONG> meanRssi{0};
        std::atomic<DWORD> unlockKey{VK_RETURN};
        std::thread pipeServer(ServeProximity, std::wstring(pipeName),
            std::cref(lastEligible), std::cref(meanRssi), std::cref(unlockKey));
        SetConsoleCtrlHandler(StopOnConsoleEvent, TRUE);
        winrt::init_apartment();
        BluetoothLEAdvertisementWatcher watcher;
        watcher.ScanningMode(BluetoothLEScanningMode::Active);
        std::mutex observationsMutex;
        std::map<uint64_t, SeenDevice> seen;
        auto received = watcher.Received([&](auto const&, BluetoothLEAdvertisementReceivedEventArgs const& event)
        {
            std::wstring name = CleanName(event.Advertisement().LocalName().c_str());
            const uint64_t address = event.BluetoothAddress();
            const ULONGLONG tick = GetTickCount64();
            std::lock_guard<std::mutex> lock(observationsMutex);
            auto& entry = seen[address];
            entry.rssi = event.RawSignalStrengthInDBm();
            entry.tick = tick;
            if (!name.empty()) entry.name = std::move(name);
        });
        watcher.Start();
        LogEvent("monitor_started");
        ULONGLONG farSince = 0;
        bool lockedThisAbsence = false;
        bool priorNear = false;
        ULONGLONG lastSnapshot = 0;
        ULONGLONG lastPairedRefresh = 0;
        ULONGLONG lastLanProbe = 0;
        bool lanPresent = false;
        std::vector<Settings::LanDevice> probedLanDevices;
        int lanOnline = 0;
        while (running)
        {
            const ULONGLONG now = GetTickCount64();
            if (!lastPairedRefresh || now - lastPairedRefresh >= 60000)
            {
                // Refresh the identity cache independently of the live RSSI observations.
                try
                {
                    std::string list;
                    for (const auto& [address, name] : PairedBleDevices())
                        list += Utf8(FormatBluetoothAddress(address)) + "\t" + Utf8(name) + "\n";
                    WriteText(DataDirectory() + L"\\paired.tsv", list);
                }
                catch (const winrt::hresult_error&) {}
                lastPairedRefresh = now;
            }
            const Settings settings = LoadSettings();
            unlockKey = static_cast<DWORD>(settings.unlockKey);
            std::vector<Observation> observations;
            std::vector<std::pair<uint64_t, SeenDevice>> discovered;
            {
                std::lock_guard<std::mutex> lock(observationsMutex);
                for (auto it = seen.begin(); it != seen.end();)
                {
                    if (now - it->second.tick > 30000) it = seen.erase(it);
                    else
                    {
                        observations.push_back({it->first, it->second.rssi, it->second.tick});
                        discovered.push_back(*it);
                        ++it;
                    }
                }
            }
            const Decision decision = EvaluateProximity(settings, observations, now);
            if (settings.unlockMode == 0)
            {
                lanPresent = false;
                lanOnline = 0;
                lastLanProbe = 0;
            }
            else if (!lastLanProbe || now - lastLanProbe >= 10000 ||
                settings.lanDevices != probedLanDevices)
            {
                lanOnline = 0;
                for (const auto& device : settings.lanDevices)
                    if (ProbeLanDevice(device.ipv4, device.mac)) ++lanOnline;
                lanPresent = AllLanDevicesOnline(static_cast<int>(settings.lanDevices.size()), lanOnline);
                lastLanProbe = now;
                probedLanDevices = settings.lanDevices;
            }
            const bool withinUnlockRange = settings.automaticUnlock &&
                UnlockConditionMet(settings.unlockMode, decision.unlock, lanPresent);
            if (withinUnlockRange)
            {
                lastEligible = settings.unlockMode == 0 ? decision.newestTick : now;
                meanRssi = settings.unlockMode == 1 ? -50 : decision.meanRssi;
            }
            else
            {
                lastEligible = 0;
                meanRssi = 0;
            }
            if (withinUnlockRange != priorNear)
            {
                LogEvent(withinUnlockRange ? "unlock_range_entered" : "unlock_range_left");
                priorNear = withinUnlockRange;
            }
            // Automatic locking keeps its original Bluetooth rule in every
            // unlock mode; an idle iPhone Wi-Fi radio must not lock the PC.
            if (settings.automaticLock && !settings.devices.empty() && decision.outsideLockRange)
            {
                if (!farSince) farSince = now;
                if (!lockedThisAbsence && now - farSince >= settings.lockDelaySeconds * 1000ULL)
                {
                    if (LockWorkStation())
                    {
                        LogEvent("automatic_lock");
                        lockedThisAbsence = true;
                    }
                }
            }
            else
            {
                farSince = 0;
                lockedThisAbsence = false;
            }
            if (now - lastSnapshot >= 4000)
            {
                char status[384];
                sprintf_s(status,
                    "running=1\nselected=%zu\ndetected=%d\nmean=%d\nnear=%d\n"
                    "automatic_lock=%d\nlast_signal_tick=%llu\nmode=%d\nbluetooth_near=%d\nlan_present=%d\n"
                    "unlock_threshold=%d\nlock_threshold=%d\nlan_online=%d\nlan_selected=%zu\n",
                    settings.devices.size(), decision.detected, decision.meanRssi,
                    withinUnlockRange ? 1 : 0, settings.automaticLock ? 1 : 0,
                    static_cast<unsigned long long>(decision.newestTick),
                    settings.unlockMode, decision.unlock ? 1 : 0, lanPresent ? 1 : 0,
                    settings.unlockThreshold, settings.lockThreshold, lanOnline, settings.lanDevices.size());
                WriteText(DataDirectory() + L"\\status.txt", status);
                std::string list;
                std::sort(discovered.begin(), discovered.end(), [](const auto& left, const auto& right)
                { return left.second.tick > right.second.tick; });
                for (size_t i = 0; i < discovered.size() && i < 80; ++i)
                {
                    list += Utf8(FormatBluetoothAddress(discovered[i].first)) + "\t" +
                        std::to_string(discovered[i].second.rssi) + "\t" +
                        Utf8(CleanName(discovered[i].second.name)) + "\t" +
                        std::to_string((now - discovered[i].second.tick) / 1000) + "\n";
                }
                WriteText(DataDirectory() + L"\\discovered.tsv", list);
                lastSnapshot = now;
            }
            Sleep(2000);
        }
        watcher.Stop();
        watcher.Received(received);
        HANDLE wake = CreateFileW(pipeName, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
        pipeServer.join();
        LogEvent("monitor_stopped");
        CloseHandle(singleton);
        return 0;
    }

    int RunCommand(int argc, wchar_t** argv)
    {
        if (argc == 2 && wcscmp(argv[1], L"--list-lan") == 0)
        {
            for (const auto& device : ScanLanDevices())
                printf("%s\t%s\n", Utf8(device.ipv4).c_str(), Utf8(FormatBluetoothAddress(device.mac)).c_str());
            return 0;
        }
        if (argc == 3 && wcscmp(argv[1], L"--scan-lan") == 0)
        {
            uint64_t mac = 0;
            std::wstring ipv4;
            if (!ParseBluetoothAddress(argv[2], mac) || mac == 0) return 2;
            if (!DiscoverLanDevice(mac, ipv4)) return 1;
            printf("%s\t%s\n", Utf8(ipv4).c_str(), Utf8(FormatBluetoothAddress(mac)).c_str());
            return 0;
        }
        if (argc == 4 && wcscmp(argv[1], L"--resolve-lan") == 0)
        {
            std::wstring ipv4 = argv[2];
            uint64_t mac = 0;
            if ((!ipv4.empty() && !IsValidLanIpv4(ipv4)) ||
                (*argv[3] && !ParseBluetoothAddress(argv[3], mac))) return 2;
            if (!ResolveLanDevice(ipv4, mac)) return 1;
            printf("%s\t%s\n", Utf8(ipv4).c_str(), Utf8(FormatBluetoothAddress(mac)).c_str());
            return 0;
        }
        if (argc == 4 && wcscmp(argv[1], L"--probe-lan") == 0)
        {
            uint64_t mac = 0;
            if (!ParseBluetoothAddress(argv[3], mac) || !IsValidLanIpv4(argv[2])) return 2;
            return ProbeLanDevice(argv[2], mac) ? 0 : 1;
        }
        if (argc == 2 && wcscmp(argv[1], L"--lock") == 0)
            return LockWorkStation() ? 0 : 1;
        if (argc == 3 && wcscmp(argv[1], L"--startup") == 0)
        {
            if (wcscmp(argv[2], L"0") != 0 && wcscmp(argv[2], L"1") != 0) return 2;
            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                KEY_SET_VALUE, &key) != ERROR_SUCCESS) return 1;
            LONG result;
            if (wcscmp(argv[2], L"1") == 0)
            {
                wchar_t executable[MAX_PATH];
                if (!GetModuleFileNameW(nullptr, executable, MAX_PATH))
                {
                    RegCloseKey(key);
                    return 1;
                }
                const std::wstring command = L"\"" + std::wstring(executable) + L"\" --run";
                result = RegSetValueExW(key, L"BluetoothUnlock", 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(command.c_str()),
                    static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
            }
            else result = RegDeleteValueW(key, L"BluetoothUnlock");
            RegCloseKey(key);
            return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND ? 0 : 1;
        }
        Settings settings = LoadSettings();
        if ((argc == 7 && wcscmp(argv[1], L"--configure") == 0) ||
            (argc == 10 && wcscmp(argv[1], L"--configure-v2") == 0) ||
            (argc == 11 && wcscmp(argv[1], L"--configure-v3") == 0) ||
            (argc == 13 && wcscmp(argv[1], L"--configure-v4") == 0))
        {
            long values[5];
            for (int i = 0; i < 5; ++i)
            {
                wchar_t* end = nullptr;
                values[i] = wcstol(argv[i + 2], &end, 10);
                if (!end || *end) return 2;
            }
            if ((values[3] != 0 && values[3] != 1) ||
                (values[4] != 0 && values[4] != 1)) return 2;
            settings.unlockThreshold = values[0];
            settings.lockThreshold = values[1];
            settings.lockDelaySeconds = values[2];
            settings.automaticLock = values[3] != 0;
            settings.automaticUnlock = values[4] != 0;
            if (argc >= 10)
            {
                wchar_t* end = nullptr;
                const long mode = wcstol(argv[7], &end, 10);
                if (!end || *end || mode < 0 || mode > 2) return 2;
                settings.unlockMode = static_cast<int>(mode);
                Settings::LanDevice device{argv[8], 0};
                if (*argv[9] && !ParseBluetoothAddress(argv[9], device.mac)) return 2;
                if (!device.ipv4.empty() || device.mac)
                {
                    if (!IsValidLanIpv4(device.ipv4) || device.mac == 0) return 2;
                    if (settings.lanDevices.empty()) settings.lanDevices.push_back(device);
                    else settings.lanDevices[0] = device;
                }
                else settings.lanDevices.clear();
                if (argc == 13)
                {
                    settings.lanDevices.resize(settings.lanDevices.empty() ? 0 : 1);
                    if (*argv[11] || *argv[12])
                    {
                        Settings::LanDevice second{argv[11], 0};
                        if (settings.lanDevices.empty() || !IsValidLanIpv4(second.ipv4) ||
                            !ParseBluetoothAddress(argv[12], second.mac) || second.mac == 0) return 2;
                        settings.lanDevices.push_back(second);
                    }
                }
                if (settings.unlockMode != 0 && settings.lanDevices.empty()) return 2;
            }
            if (argc >= 11)
            {
                wchar_t* end = nullptr;
                const long key = wcstol(argv[10], &end, 10);
                if (!end || *end || (key != VK_RETURN && (key < 'A' || key > 'Z'))) return 2;
                settings.unlockKey = static_cast<int>(key);
            }
        }
        else if (argc == 4 && wcscmp(argv[1], L"--add") == 0)
        {
            uint64_t address;
            if (!ParseBluetoothAddress(argv[2], address)) return 2;
            auto found = std::find_if(settings.devices.begin(), settings.devices.end(),
                [&](const SelectedDevice& item) { return item.address == address; });
            if (found == settings.devices.end()) settings.devices.push_back({address, CleanName(argv[3])});
            else found->name = CleanName(argv[3]);
        }
        else if (argc == 3 && wcscmp(argv[1], L"--remove") == 0)
        {
            uint64_t address;
            if (!ParseBluetoothAddress(argv[2], address)) return 2;
            settings.devices.erase(std::remove_if(settings.devices.begin(), settings.devices.end(),
                [&](const SelectedDevice& item) { return item.address == address; }),
                settings.devices.end());
        }
        else if (argc == 4 && wcscmp(argv[1], L"--set") == 0)
        {
            wchar_t* end = nullptr;
            const long value = wcstol(argv[3], &end, 10);
            if (!end || *end) return 2;
            if (wcscmp(argv[2], L"unlock") == 0) settings.unlockThreshold = value;
            else if (wcscmp(argv[2], L"lock") == 0) settings.lockThreshold = value;
            else if (wcscmp(argv[2], L"delay") == 0) settings.lockDelaySeconds = value;
            else if (wcscmp(argv[2], L"autolock") == 0) settings.automaticLock = value != 0;
            else if (wcscmp(argv[2], L"autounlock") == 0) settings.automaticUnlock = value != 0;
            else return 2;
        }
        else return 2;
        return SaveSettings(settings) ? 0 : 1;
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc == 2 && wcscmp(argv[1], L"--list-paired-ble") == 0)
    {
        winrt::init_apartment();
        for (const auto& [address, name] : PairedBleDevices())
            printf("%s\t%s\n", Utf8(FormatBluetoothAddress(address)).c_str(), Utf8(name).c_str());
        return 0;
    }
    if (argc == 1 || (argc == 2 && wcscmp(argv[1], L"--run") == 0))
        return RunMonitor();
    return RunCommand(argc, argv);
}
