using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using System.Net;
using System.Net.Sockets;

namespace BluetoothUnlock.UI;

public partial class MainWindow : Window
{
    private readonly BackendClient _backend = new();
    private readonly DispatcherTimer _refreshTimer = new() { Interval = TimeSpan.FromSeconds(4) };

    public MainWindow()
    {
        InitializeComponent();
        Loaded += OnLoaded;
        Closed += (_, _) => _refreshTimer.Stop();
        UnlockSlider.ValueChanged += (_, _) => UnlockValue.Text = $"{UnlockSlider.Value:0} dBm";
        LockSlider.ValueChanged += (_, _) => LockValue.Text = $"{LockSlider.Value:0} dBm";
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            _backend.EnsureRunning();
            var settings = _backend.ReadSettings();
            UnlockSlider.Value = settings.UnlockThreshold;
            LockSlider.Value = settings.LockThreshold;
            AutoUnlockCheck.IsChecked = settings.AutomaticUnlock;
            AutoLockCheck.IsChecked = settings.AutomaticLock;
            ModeBox.SelectedIndex = settings.UnlockMode;
            LanIpBox.Text = settings.LanIp;
            LanMacBox.Text = settings.LanMac;
            StartupCheck.IsChecked = _backend.StartupEnabled();
            DelayBox.SelectedItem = DelayBox.Items.OfType<ComboBoxItem>()
                .FirstOrDefault(item => item.Tag?.ToString() == settings.LockDelaySeconds.ToString())
                ?? DelayBox.Items[1];
            RefreshSelectedDevices();
            RefreshStatus();
            _refreshTimer.Tick += (_, _) => RefreshStatus();
            _refreshTimer.Start();
        }
        catch (Exception error) { ShowError(error); }
    }

    private void RefreshSelectedDevices()
    {
        SelectedList.ItemsSource = _backend.ReadSettings().Devices;
    }

    private void RefreshStatus()
    {
        try
        {
            var selectedAddress = (NearbyList.SelectedItem as DeviceRow)?.Address;
            var discovered = _backend.ReadDiscovered();
            NearbyList.ItemsSource = discovered;
            NearbyList.SelectedItem = discovered.FirstOrDefault(item => item.Address == selectedAddress);
            var status = _backend.ReadStatus();
            var online = _backend.IsRunning;
            var detected = status.GetValueOrDefault("detected", "0");
            var selected = status.GetValueOrDefault("selected", "0");
            var signal = status.GetValueOrDefault("mean", "0");
            var withinRange = status.GetValueOrDefault("near", "0") == "1";
            StatusText.Text = !online ? "后台未运行" : withinRange ? "解锁条件已满足" : "等待解锁条件";
            MeanText.Text = detected == "0" ? "-- dBm" : $"{signal} dBm";
            DeviceCountText.Text = $"已检测 {detected} / 已选择 {selected} 台设备";
            BluetoothStateText.Text = status.GetValueOrDefault("bluetooth_near", "0") == "1"
                ? "蓝牙：已达到阈值" : "蓝牙：未达到阈值";
            LanStateText.Text = status.GetValueOrDefault("mode", "0") == "0"
                ? "局域网：当前模式未启用"
                : status.GetValueOrDefault("lan_present", "0") == "1"
                    ? "局域网：已找到匹配设备" : "局域网：未找到匹配设备";
            EventList.ItemsSource = _backend.ReadEvents().Select(TranslateEvent).ToArray();
        }
        catch (Exception error) { HintText.Text = $"读取状态失败：{error.Message}"; }
    }

    private static string TranslateEvent(string line) => line
        .Replace("monitor_started", "后台已启动")
        .Replace("monitor_stopped", "后台已停止")
        .Replace("unlock_range_entered", "进入解锁范围")
        .Replace("unlock_range_left", "离开解锁范围")
        .Replace("automatic_lock", "已自动锁定");

    private async void OnAddDevice(object sender, RoutedEventArgs e)
    {
        if (NearbyList.SelectedItem is not DeviceRow device)
        {
            HintText.Text = "请先在附近扫描结果中选择一台设备。";
            return;
        }
        try
        {
            await _backend.RunCommandAsync("--add", device.Address, device.Name);
            RefreshSelectedDevices();
            HintText.Text = "设备已加入。若设备地址会变化，请先验证它能持续被识别。";
        }
        catch (Exception error) { ShowError(error); }
    }

    private async void OnRemoveDevice(object sender, RoutedEventArgs e)
    {
        if (SelectedList.SelectedItem is not DeviceRow device)
        {
            HintText.Text = "请先选择要移除的设备。";
            return;
        }
        try
        {
            await _backend.RunCommandAsync("--remove", device.Address);
            RefreshSelectedDevices();
            HintText.Text = "设备已移除。";
        }
        catch (Exception error) { ShowError(error); }
    }

    private async void OnSaveSettings(object sender, RoutedEventArgs e)
    {
        var unlock = (int)UnlockSlider.Value;
        var lockAt = (int)LockSlider.Value;
        if (lockAt > unlock)
        {
            HintText.Text = "锁定阈值应低于或等于解锁阈值。";
            return;
        }
        var delay = (DelayBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "60";
        var mode = Math.Max(0, ModeBox.SelectedIndex);
        var ip = LanIpBox.Text.Trim();
        var mac = LanMacBox.Text.Trim().Replace(":", "").Replace("-", "").ToUpperInvariant();
        if ((ip.Length > 0 && (!IPAddress.TryParse(ip, out var address) ||
                               address.AddressFamily != AddressFamily.InterNetwork)) ||
            (mac.Length > 0 && (mac.Length != 12 || !mac.All(Uri.IsHexDigit))) ||
            (mode != 0 && (ip.Length == 0 || mac.Length != 12)))
        {
            HintText.Text = "局域网模式需要有效的 IPv4 地址和 12 位 Wi‑Fi MAC。";
            return;
        }
        try
        {
            await _backend.RunCommandAsync("--configure-v2", unlock.ToString(), lockAt.ToString(),
                delay, AutoLockCheck.IsChecked == true ? "1" : "0",
                AutoUnlockCheck.IsChecked == true ? "1" : "0", mode.ToString(), ip, mac);
            await _backend.RunCommandAsync("--startup", StartupCheck.IsChecked == true ? "1" : "0");
            HintText.Text = "设置已保存，后台会自动读取新规则。";
        }
        catch (Exception error) { ShowError(error); }
    }

    private async void OnManualLock(object sender, RoutedEventArgs e)
    {
        try { await _backend.RunCommandAsync("--lock"); }
        catch (Exception error) { ShowError(error); }
    }

    private void ShowError(Exception error)
    {
        HintText.Text = error.Message;
        MessageBox.Show(this, error.Message, "蓝牙靠近解锁", MessageBoxButton.OK,
            MessageBoxImage.Warning);
    }
}
