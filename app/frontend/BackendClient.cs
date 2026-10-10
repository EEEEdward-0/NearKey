using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;

namespace BluetoothUnlock.UI;

internal sealed record DeviceRow(string Address, string Name, int Rssi, int AgeSeconds = -1,
    bool IsSelected = false, bool MissingFromScan = false, bool IsPaired = false)
{
    public string SignalText => MissingFromScan ? "未检测到" : Rssi == 0 ? "" : $"{Rssi} dBm";
    public string DisplayAddress => Address.Length == 12 ?
        string.Join(":", Enumerable.Range(0, 6).Select(index => Address.Substring(index * 2, 2))) : Address;
    public string DeviceDetail => MissingFromScan ? $"MAC {DisplayAddress}\n最近 30 秒未收到广播" :
        AgeSeconds < 0 ? $"MAC {DisplayAddress}" :
        $"MAC {DisplayAddress}\n{AgeSeconds} 秒前收到广播";
    public string IdentityText => IsSelected ? "已选择" : IsPaired ? "Windows 已配对" : "身份未确认";
}

internal sealed record LanDeviceRow(string Ip, string Mac);

internal sealed record BackendSettings(int UnlockThreshold, int LockThreshold,
    int LockDelaySeconds, bool AutomaticLock, bool AutomaticUnlock, int UnlockKey, int UnlockMode,
    IReadOnlyList<LanDeviceRow> LanDevices,
    IReadOnlyList<DeviceRow> Devices);

internal sealed class BackendClient
{
    private readonly string _executable;
    private readonly string _dataDirectory = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "BluetoothUnlock");

    private readonly string _runtimeDirectory;

    public BackendClient()
    {
        var alongsideUi = Path.Combine(AppContext.BaseDirectory, "BluetoothBackend.exe");
        var development = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory,
            "..", "..", "..", "..", "backend", "x64", "Release", "BluetoothBackend.exe"));
        using var serviceKey = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\BluetoothUnlockApp");
        var ownSid = System.Security.Principal.WindowsIdentity.GetCurrent().User?.Value;
        var serviceInstalled = serviceKey?.GetValue("ServiceEnabled") is int enabled && enabled == 1;
        if (serviceInstalled && (serviceKey?.GetValue("UserSid") as string) != ownSid)
            throw new InvalidOperationException("此服务绑定了另一个 Windows 账户，请使用已配置的账户打开设置。");
        if (serviceInstalled && serviceKey?.GetValue("DataDirectory") is string shared)
            _dataDirectory = shared;
        _runtimeDirectory = serviceInstalled && serviceKey?.GetValue("RuntimeDirectory") is string runtime
            ? runtime : _dataDirectory;
        _executable = File.Exists(alongsideUi) ? alongsideUi : development;
        if (!File.Exists(_executable)) throw new FileNotFoundException("找不到蓝牙后台程序。", _executable);
    }

    public bool ServiceInstalled
    {
        get
        {
            using var key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\BluetoothUnlockApp");
            return key?.GetValue("ServiceEnabled") is int enabled && enabled == 1;
        }
    }

    public bool IsRunning
    {
        get
        {
            if (!ServiceInstalled) return Process.GetProcessesByName("BluetoothBackend").Length != 0;
            var status = ReadStatus();
            return status.GetValueOrDefault("service_monitor", "0") == "1" &&
                long.TryParse(status.GetValueOrDefault("snapshot_tick", "0"), out var tick) &&
                tick > 0 && tick <= Environment.TickCount64 && Environment.TickCount64 - tick <= 15000;
        }
    }

    public void EnsureRunning()
    {
        using var key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\BluetoothUnlockApp");
        var serviceInstalled = key?.GetValue("ServiceEnabled") is int enabled && enabled == 1;
        if (!serviceInstalled && IsRunning) return;
        Process.Start(new ProcessStartInfo(_executable, serviceInstalled ? "--session" : "--run")
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            WindowStyle = ProcessWindowStyle.Hidden
        });
    }

    public async Task<string> RunCommandAsync(params string[] arguments)
    {
        var start = new ProcessStartInfo(_executable)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardError = true,
            RedirectStandardOutput = true,
            StandardOutputEncoding = Encoding.UTF8
        };
        foreach (var argument in arguments) start.ArgumentList.Add(argument);
        using var process = Process.Start(start) ?? throw new InvalidOperationException("无法启动后台命令。 ");
        var output = process.StandardOutput.ReadToEndAsync();
        await process.WaitForExitAsync();
        if (process.ExitCode != 0) throw new InvalidOperationException($"设置未保存，后台返回代码 {process.ExitCode}。");
        return await output;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetPrivateProfileStringW")]
    private static extern uint GetPrivateProfileString(string section, string key,
        string defaultValue, StringBuilder result, uint size, string fileName);

    private string ReadIni(string section, string key, string fallback)
    {
        var buffer = new StringBuilder(256);
        GetPrivateProfileString(section, key, fallback, buffer,
            (uint)buffer.Capacity, Path.Combine(_dataDirectory, "settings.ini"));
        return buffer.ToString();
    }

    private int ReadInt(string section, string key, int fallback) =>
        int.TryParse(ReadIni(section, key, fallback.ToString()), out var value) ? value : fallback;

    public BackendSettings ReadSettings()
    {
        var version = ReadInt("Meta", "Version", 0);
        if (version < 0 || version > 2)
            throw new InvalidOperationException("配置版本高于当前程序支持范围或无效，请更新程序。配置未修改。");
        var unlockKey = ReadInt("Behavior", "UnlockKey", 13);
        if (unlockKey != 13 && (unlockKey < 'A' || unlockKey > 'Z')) unlockKey = 13;
        var devices = new List<DeviceRow>();
        var count = Math.Clamp(ReadInt("Devices", "Count", 0), 0, 8);
        for (var index = 0; index < count; index++)
        {
            var address = ReadIni("Devices", $"Address{index}", "");
            if (address.Length == 12)
                devices.Add(new DeviceRow(address, ReadIni("Devices", $"Name{index}", "设备"), 0,
                    IsSelected: true));
        }
        var lanDevices = new List<LanDeviceRow>();
        var lanCount = ReadInt("LAN", "Count", -1);
        for (var index = 0; index < (lanCount < 0 ? 1 : Math.Min(lanCount, 2)); index++)
        {
            var ip = ReadIni("LAN", lanCount < 0 ? "IPv4" : $"IPv4{index}", "");
            var mac = ReadIni("LAN", lanCount < 0 ? "Mac" : $"Mac{index}", "");
            if (lanCount >= 0 || ip.Length > 0 || mac.Length > 0)
                lanDevices.Add(new LanDeviceRow(ip, mac));
        }
        return new BackendSettings(ReadInt("Signal", "UnlockThreshold", -65),
            ReadInt("Signal", "LockThreshold", -80),
            ReadInt("Signal", "LockDelaySeconds", 60),
            ReadInt("Behavior", "AutomaticLock", 0) != 0,
            ReadInt("Behavior", "AutomaticUnlock", 1) != 0,
            unlockKey,
            Math.Clamp(ReadInt("Behavior", "UnlockMode", 0), 0, 2),
            lanDevices, devices);
    }

    public IReadOnlyList<DeviceRow> ReadDiscovered()
    {
        var path = Path.Combine(_runtimeDirectory, "discovered.tsv");
        try
        {
            var selectedDevices = ReadSettings().Devices;
            var selectedNames = selectedDevices.ToDictionary(
                device => device.Address, device => device.Name,
                StringComparer.OrdinalIgnoreCase);
            var pairedPath = Path.Combine(_runtimeDirectory, "paired.tsv");
            var pairedNames = (File.Exists(pairedPath) ? File.ReadLines(pairedPath, Encoding.UTF8) : [])
                .Select(line => line.Split('\t'))
                .Where(parts => parts.Length == 2 && parts[0].Length == 12)
                .GroupBy(parts => parts[0], StringComparer.OrdinalIgnoreCase)
                .ToDictionary(group => group.Key, group => group.Last()[1],
                    StringComparer.OrdinalIgnoreCase);
            var discovered = (File.Exists(path) ? File.ReadLines(path, Encoding.UTF8) : [])
                .Select(line => line.Split('\t'))
                .Where(parts => parts.Length >= 3 && parts[0].Length == 12 &&
                                int.TryParse(parts[1], out _))
                .Select(parts => new DeviceRow(parts[0],
                    selectedNames.GetValueOrDefault(parts[0],
                        pairedNames.GetValueOrDefault(parts[0],
                            string.IsNullOrWhiteSpace(parts[2]) ? "未命名 BLE 设备" : parts[2])),
                    int.Parse(parts[1]), parts.Length > 3 && int.TryParse(parts[3], out var age)
                        ? age : -1, selectedNames.ContainsKey(parts[0]),
                    IsPaired: pairedNames.ContainsKey(parts[0])))
                .ToArray();
            var seenAddresses = discovered.Select(device => device.Address)
                .ToHashSet(StringComparer.OrdinalIgnoreCase);
            // Keep saved and paired devices visible while absent, without inventing live RSSI.
            return discovered.Concat(selectedDevices
                    .Where(device => !seenAddresses.Contains(device.Address))
                    .Select(device => device with
                    {
                        MissingFromScan = true,
                        IsPaired = pairedNames.ContainsKey(device.Address)
                    }))
                .Concat(pairedNames.Where(pair => !seenAddresses.Contains(pair.Key) &&
                        !selectedNames.ContainsKey(pair.Key))
                    .Select(pair => new DeviceRow(pair.Key, pair.Value, 0,
                        MissingFromScan: true, IsPaired: true)))
                .OrderByDescending(device => device.IsSelected)
                .ThenByDescending(device => device.IsPaired)
                .ThenBy(device => device.MissingFromScan)
                .ThenBy(device => device.AgeSeconds)
                .ToArray();
        }
        catch (IOException) { return []; }
    }

    public IReadOnlyDictionary<string, string> ReadStatus()
    {
        var path = Path.Combine(_runtimeDirectory, "status.txt");
        if (!File.Exists(path)) return new Dictionary<string, string>();
        try
        {
            return File.ReadLines(path).Select(line => line.Split('=', 2))
                .Where(parts => parts.Length == 2)
                .ToDictionary(parts => parts[0], parts => parts[1]);
        }
        catch (IOException) { return new Dictionary<string, string>(); }
    }

    public IReadOnlyList<string> ReadEvents()
    {
        try
        {
            var paths = ServiceInstalled
                ? new[] { Path.Combine(_runtimeDirectory, "events.log"), Path.Combine(_dataDirectory, "session-events.log") }
                : new[] { Path.Combine(_runtimeDirectory, "events.log") };
            return paths.Where(File.Exists).SelectMany(path => File.ReadLines(path).TakeLast(30))
                .OrderByDescending(line => line, StringComparer.Ordinal).Take(30).ToArray();
        }
        catch (IOException) { return []; }
    }

    public IEnumerable<(string Name, string Path)> DiagnosticFiles()
    {
        var installDirectory = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "NearKey");
        yield return ("installer.log", Path.Combine(installDirectory, "installer.log"));
        yield return ("install.log", Path.Combine(installDirectory, "install.log"));
        yield return ("ui-errors.previous.log", Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "BluetoothUnlock", "ui-errors.previous.log"));
        yield return ("service-events.log", Path.Combine(_runtimeDirectory, "events.log"));
        yield return ("service-events.previous.log", Path.Combine(_runtimeDirectory, "events.previous.log"));
        if (ServiceInstalled)
        {
            yield return ("session-events.log", Path.Combine(_dataDirectory, "session-events.log"));
            yield return ("session-events.previous.log", Path.Combine(_dataDirectory, "session-events.previous.log"));
        }
    }

    public bool StartupEnabled()
    {
        if (ServiceInstalled) return true;
        using var runKey = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run");
        return runKey?.GetValue("BluetoothUnlock") is string;
    }
}
