using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;

namespace BluetoothUnlock.UI;

internal sealed record DeviceRow(string Address, string Name, int Rssi)
{
    public string SignalText => Rssi == 0 ? "" : $"{Rssi} dBm";
}

internal sealed record BackendSettings(int UnlockThreshold, int LockThreshold,
    int LockDelaySeconds, bool AutomaticLock, bool AutomaticUnlock,
    IReadOnlyList<DeviceRow> Devices);

internal sealed class BackendClient
{
    private readonly string _executable;
    private readonly string _dataDirectory = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "BluetoothUnlock");

    public BackendClient()
    {
        var alongsideUi = Path.Combine(AppContext.BaseDirectory, "BluetoothBackend.exe");
        var development = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory,
            "..", "..", "..", "..", "backend", "x64", "Release", "BluetoothBackend.exe"));
        _executable = File.Exists(alongsideUi) ? alongsideUi : development;
        if (!File.Exists(_executable)) throw new FileNotFoundException("找不到蓝牙后台程序。", _executable);
    }

    public bool IsRunning => Process.GetProcessesByName("BluetoothBackend").Length != 0;

    public void EnsureRunning()
    {
        if (IsRunning) return;
        Process.Start(new ProcessStartInfo(_executable, "--run")
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            WindowStyle = ProcessWindowStyle.Hidden
        });
    }

    public async Task RunCommandAsync(params string[] arguments)
    {
        var start = new ProcessStartInfo(_executable)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardError = true
        };
        foreach (var argument in arguments) start.ArgumentList.Add(argument);
        using var process = Process.Start(start) ?? throw new InvalidOperationException("无法启动后台命令。 ");
        await process.WaitForExitAsync();
        if (process.ExitCode != 0) throw new InvalidOperationException($"设置未保存，后台返回代码 {process.ExitCode}。");
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
        var devices = new List<DeviceRow>();
        var count = Math.Clamp(ReadInt("Devices", "Count", 0), 0, 8);
        for (var index = 0; index < count; index++)
        {
            var address = ReadIni("Devices", $"Address{index}", "");
            if (address.Length == 12)
                devices.Add(new DeviceRow(address, ReadIni("Devices", $"Name{index}", "设备"), 0));
        }
        return new BackendSettings(ReadInt("Signal", "UnlockThreshold", -65),
            ReadInt("Signal", "LockThreshold", -80),
            ReadInt("Signal", "LockDelaySeconds", 60),
            ReadInt("Behavior", "AutomaticLock", 0) != 0,
            ReadInt("Behavior", "AutomaticUnlock", 1) != 0, devices);
    }

    public IReadOnlyList<DeviceRow> ReadDiscovered()
    {
        var path = Path.Combine(_dataDirectory, "discovered.tsv");
        if (!File.Exists(path)) return [];
        try
        {
            var selectedNames = ReadSettings().Devices.ToDictionary(
                device => device.Address, device => device.Name,
                StringComparer.OrdinalIgnoreCase);
            return File.ReadLines(path, Encoding.UTF8).Select(line => line.Split('\t'))
                .Where(parts => parts.Length == 3 && parts[0].Length == 12 &&
                                int.TryParse(parts[1], out _))
                .Select(parts => new DeviceRow(parts[0],
                    selectedNames.GetValueOrDefault(parts[0],
                        string.IsNullOrWhiteSpace(parts[2]) ? "未命名蓝牙设备" : parts[2]),
                    int.Parse(parts[1]))).ToArray();
        }
        catch (IOException) { return []; }
    }

    public IReadOnlyDictionary<string, string> ReadStatus()
    {
        var path = Path.Combine(_dataDirectory, "status.txt");
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
        var path = Path.Combine(_dataDirectory, "events.log");
        if (!File.Exists(path)) return [];
        try { return File.ReadLines(path).TakeLast(30).Reverse().ToArray(); }
        catch (IOException) { return []; }
    }

    public bool StartupEnabled()
    {
        using var runKey = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run");
        return runKey?.GetValue("BluetoothUnlock") is string;
    }
}
