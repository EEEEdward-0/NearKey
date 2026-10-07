#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <pwd.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
std::atomic<bool> running{true};

struct Config {
    struct LanDevice { std::string ip; std::string mac; };
    std::string user;
    std::vector<std::string> devices;
    std::vector<LanDevice> lanDevices;
    int mode = 0; // 0: Bluetooth, 1: LAN, 2: both.
    int unlockThreshold = -65;
    int lockThreshold = -80;
    int lockDelaySeconds = 60;
    bool automaticLock = false;
    bool automaticUnlock = true;
};

void Stop(int) { running = false; }

std::string Trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string Upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

bool ValidAddress(const std::string& address) {
    if (address.size() != 17) return false;
    for (size_t i = 0; i < address.size(); ++i)
        if (i % 3 == 2 ? address[i] != ':' : !std::isxdigit(static_cast<unsigned char>(address[i]))) return false;
    return true;
}

bool ValidIpv4(const std::string& address) {
    in_addr parsed{};
    return inet_pton(AF_INET, address.c_str(), &parsed) == 1;
}

Config LoadConfig(const std::string& path) {
    struct stat fileInfo{};
    if (stat(path.c_str(), &fileInfo) != 0 || !S_ISREG(fileInfo.st_mode) ||
        fileInfo.st_uid != 0 || (fileInfo.st_mode & 022) != 0)
        throw std::runtime_error("Config must be a root-owned, non-writable-by-others file");
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot read config: " + path);
    Config config;
    std::string line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto split = line.find('=');
        if (split == std::string::npos) throw std::runtime_error("Invalid config line");
        const auto key = Trim(line.substr(0, split));
        const auto value = Trim(line.substr(split + 1));
        if (key == "user") config.user = value;
        else if (key == "mode") {
            if (value == "bluetooth") config.mode = 0;
            else if (value == "lan") config.mode = 1;
            else if (value == "both") config.mode = 2;
            else throw std::runtime_error("Invalid mode");
        }
        else if (key == "devices") {
            std::istringstream items(value);
            std::string address;
            while (std::getline(items, address, ',')) {
                address = Upper(Trim(address));
                if (!ValidAddress(address)) throw std::runtime_error("Invalid device address");
                config.devices.push_back(address);
            }
        } else if (key == "lan_devices") {
            std::istringstream items(value);
            std::string item;
            while (std::getline(items, item, ',')) {
                const auto splitDevice = item.find('@');
                if (splitDevice == std::string::npos) throw std::runtime_error("Invalid LAN device");
                Config::LanDevice device{Trim(item.substr(0, splitDevice)), Upper(Trim(item.substr(splitDevice + 1)))};
                if (!ValidIpv4(device.ip) || !ValidAddress(device.mac)) throw std::runtime_error("Invalid LAN IP or MAC");
                config.lanDevices.push_back(device);
            }
        } else if (key == "unlock_threshold") config.unlockThreshold = std::stoi(value);
        else if (key == "lock_threshold") config.lockThreshold = std::stoi(value);
        else if (key == "lock_delay_seconds") config.lockDelaySeconds = std::stoi(value);
        else if (key == "automatic_lock") config.automaticLock = value == "1";
        else if (key == "automatic_unlock") config.automaticUnlock = value == "1";
        else throw std::runtime_error("Unknown config key: " + key);
    }
    if (config.user.empty() || !getpwnam(config.user.c_str()) || config.user == "root" ||
        (config.devices.empty() && config.mode != 1) || config.devices.size() > 8 ||
        (config.mode != 0 && config.lanDevices.empty()) || config.lanDevices.size() > 2 ||
        config.lockThreshold > config.unlockThreshold ||
        config.unlockThreshold > -20 || config.lockThreshold < -100 || config.lockDelaySeconds < 10)
        throw std::runtime_error("Invalid user, devices, or thresholds in config");
    return config;
}

struct Observation { int rssi; Clock::time_point seen; };
using Observations = std::map<std::string, Observation>;

void Scan(Observations& observations) {
    FILE* scanner = popen("bluetoothctl --timeout 5 scan le 2>/dev/null", "r");
    if (!scanner) return;
    char line[512];
    while (fgets(line, sizeof(line), scanner)) {
        const std::string text(line);
        if (text.find("[CHG]") == std::string::npos) continue;
        const auto device = text.find("Device ");
        if (device == std::string::npos || text.size() < device + 29 ||
            text.compare(device + 24, 6, " RSSI:") != 0) continue;
        const auto address = Upper(text.substr(device + 7, 17));
        if (!ValidAddress(address)) continue;
        try {
            const int signal = std::stoi(text.substr(device + 30));
            if (signal >= -127 && signal <= 20) observations[address] = {signal, Clock::now()};
        } catch (const std::exception&) {}
    }
    pclose(scanner);
}

struct Decision { int detected = 0; int mean = 0; bool unlock = false; bool outsideLockRange = true; };

Decision Evaluate(const Config& config, const Observations& observations) {
    Decision result;
    int total = 0;
    const auto now = Clock::now();
    for (const auto& address : config.devices) {
        const auto found = observations.find(address);
        if (found == observations.end() || now - found->second.seen > std::chrono::seconds(20)) continue;
        ++result.detected;
        total += found->second.rssi;
    }
    if (result.detected) {
        result.mean = total / result.detected;
        result.unlock = result.mean >= config.unlockThreshold;
        result.outsideLockRange = result.mean < config.lockThreshold;
    }
    return result;
}

bool ProbeLanDevice(const Config::LanDevice& device) {
    std::system(("ping -n -c 1 -W 1 " + device.ip + " >/dev/null 2>&1").c_str());
    std::ifstream neighbors("/proc/net/arp");
    std::string line;
    std::getline(neighbors, line); // Header.
    while (std::getline(neighbors, line)) {
        std::istringstream row(line);
        std::string ip, hardware, flags, mac;
        if (!(row >> ip >> hardware >> flags >> mac)) continue;
        try {
            if (ip == device.ip && Upper(mac) == device.mac && std::stoul(flags, nullptr, 0) == 2) return true;
        } catch (const std::exception&) {}
    }
    return false;
}

bool UnlockConditionMet(int mode, bool bluetooth, bool lan) {
    if (mode == 0) return bluetooth;
    if (mode == 1) return lan;
    return bluetooth && lan;
}

bool WriteStatus(const Config& config, const Decision& decision, int lanOnline) {
    if (mkdir("/run/nearkey", 0755) != 0 && access("/run/nearkey", F_OK) != 0) return false;
    struct stat directory{};
    if (stat("/run/nearkey", &directory) != 0 || !S_ISDIR(directory.st_mode) ||
        directory.st_uid != 0 || (directory.st_mode & 022) != 0) return false;
    const std::string path = "/run/nearkey/status.tmp";
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (fd < 0) return false;
    const bool lan = !config.lanDevices.empty() && lanOnline == static_cast<int>(config.lanDevices.size());
    const std::string status = "user=" + config.user + "\nnear=" +
        (config.automaticUnlock && UnlockConditionMet(config.mode, decision.unlock, lan) ? "1" : "0") +
        "\nupdated_unix=" + std::to_string(std::time(nullptr)) + "\ndetected=" + std::to_string(decision.detected) +
        "\nmean=" + std::to_string(decision.mean) + "\nunlock_threshold=" + std::to_string(config.unlockThreshold) +
        "\nlock_threshold=" + std::to_string(config.lockThreshold) + "\nmode=" + std::to_string(config.mode) +
        "\nlan_online=" + std::to_string(lanOnline) + "\nlan_selected=" + std::to_string(config.lanDevices.size()) + "\n";
    const bool okay = write(fd, status.data(), status.size()) == static_cast<ssize_t>(status.size()) && fsync(fd) == 0;
    close(fd);
    return okay && rename(path.c_str(), "/run/nearkey/status") == 0;
}

bool LockUser(const std::string& user) {
    const auto account = getpwnam(user.c_str());
    if (!account) return false;
    FILE* sessions = popen("loginctl list-sessions --no-legend --no-pager 2>/dev/null", "r");
    if (!sessions) return false;
    bool locked = false;
    char line[256];
    while (fgets(line, sizeof(line), sessions)) {
        std::istringstream row(line);
        std::string session, name;
        unsigned uid = 0;
        if (!(row >> session >> uid >> name) || uid != account->pw_uid) continue;
        if (!std::all_of(session.begin(), session.end(), [](unsigned char c) { return std::isalnum(c); })) continue;
        locked |= std::system(("loginctl lock-session " + session + " >/dev/null 2>&1").c_str()) == 0;
    }
    pclose(sessions);
    return locked;
}

void PrintExplanation(const Config& config, const Decision& decision, int lanOnline) {
    const bool lan = !config.lanDevices.empty() && lanOnline == static_cast<int>(config.lanDevices.size());
    const bool eligible = config.automaticUnlock && UnlockConditionMet(config.mode, decision.unlock, lan);
    std::cout << "detected=" << decision.detected << '/' << config.devices.size() << " mean=";
    if (decision.detected) std::cout << decision.mean << " dBm";
    else std::cout << "none";
    std::cout << "\nbluetooth=" << (decision.unlock ? "pass" : "blocked")
              << " (threshold " << config.unlockThreshold << " dBm)"
              << "\nlan=" << lanOnline << '/' << config.lanDevices.size() << (lan ? " pass" : " blocked")
              << "\nunlock=" << (eligible ? "eligible" : "blocked")
              << "\nauto_lock=" << (config.automaticLock && decision.outsideLockRange ? "counting absence" : "not counting")
              << " (threshold " << config.lockThreshold << " dBm)\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::strcmp(argv[1], "status") == 0) {
            std::ifstream status("/run/nearkey/status");
            if (!status) throw std::runtime_error("Daemon status unavailable");
            std::cout << status.rdbuf();
            return 0;
        }
        const Config config = LoadConfig("/etc/nearkey.conf");
        if ((argc == 3 || argc == 4) && std::strcmp(argv[1], "preview") == 0) {
            const int rssi = std::stoi(argv[2]);
            if (rssi < -127 || rssi > 20) throw std::runtime_error("RSSI out of range");
            const int online = argc == 4 ? std::stoi(argv[3]) : 0;
            if (online < 0 || online > static_cast<int>(config.lanDevices.size())) throw std::runtime_error("LAN online count out of range");
            Observations simulated;
            if (!config.devices.empty()) simulated[config.devices.front()] = {rssi, Clock::now()};
            std::cout << "Simulation only; no lock or unlock action.\n";
            PrintExplanation(config, Evaluate(config, simulated), online);
            return 0;
        }
        if (argc != 2 || std::strcmp(argv[1], "run") != 0) throw std::runtime_error("Usage: nearkey run|status|preview RSSI [LAN_ONLINE]");
        if (geteuid() != 0) throw std::runtime_error("Daemon must run as root");
        std::signal(SIGTERM, Stop);
        std::signal(SIGINT, Stop);
        Observations observations;
        Clock::time_point farSince{};
        bool lockedThisAbsence = false;
        while (running) {
            Scan(observations);
            const auto decision = Evaluate(config, observations);
            int lanOnline = 0;
            if (config.mode != 0)
                for (const auto& device : config.lanDevices) lanOnline += ProbeLanDevice(device) ? 1 : 0;
            if (!WriteStatus(config, decision, lanOnline)) throw std::runtime_error("Cannot write root-owned status");
            if (config.automaticLock && decision.outsideLockRange) {
                if (farSince == Clock::time_point{}) farSince = Clock::now();
                if (!lockedThisAbsence && Clock::now() - farSince >= std::chrono::seconds(config.lockDelaySeconds))
                    lockedThisAbsence = LockUser(config.user);
            } else {
                farSince = {};
                lockedThisAbsence = false;
            }
        }
        unlink("/run/nearkey/status");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
