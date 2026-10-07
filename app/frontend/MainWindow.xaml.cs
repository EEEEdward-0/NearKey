using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using System.Net;
using System.Net.Sockets;
using Forms = System.Windows.Forms;

namespace BluetoothUnlock.UI;

public partial class MainWindow : Window
{
    private readonly BackendClient _backend = new();
    private readonly DispatcherTimer _refreshTimer = new() { Interval = TimeSpan.FromSeconds(4) };
    private readonly Forms.NotifyIcon _trayIcon;

    public MainWindow()
    {
        InitializeComponent();
        _trayIcon = new Forms.NotifyIcon
        {
            Icon = System.Drawing.SystemIcons.Application,
            Text = "靠近解锁",
            Visible = true,
            ContextMenuStrip = new Forms.ContextMenuStrip()
        };
        _trayIcon.ContextMenuStrip.Items.Add("打开设置", null, (_, _) => Dispatcher.Invoke(ShowFromTray));
        _trayIcon.ContextMenuStrip.Items.Add("立即锁定", null, (_, _) => Dispatcher.Invoke(() => OnManualLock(this, new RoutedEventArgs())));
        _trayIcon.ContextMenuStrip.Items.Add("退出设置界面", null, (_, _) => Dispatcher.Invoke(Close));
        _trayIcon.DoubleClick += (_, _) => Dispatcher.Invoke(ShowFromTray);
        StateChanged += (_, _) => { if (WindowState == WindowState.Minimized) Hide(); };
        Loaded += OnLoaded;
        Closed += (_, _) => { _refreshTimer.Stop(); _trayIcon.Visible = false; _trayIcon.Dispose(); };
        UnlockSlider.ValueChanged += (_, _) => UnlockValue.Text = $"{UnlockSlider.Value:0} dBm";
        LockSlider.ValueChanged += (_, _) => LockValue.Text = $"{LockSlider.Value:0} dBm";
    }

    private void ShowFromTray()
    {
        Show();
        WindowState = WindowState.Normal;
        Activate();
    }

    private void OnNavigate(object sender, RoutedEventArgs e)
    {
        if (sender is not System.Windows.Controls.Button { Tag: string target }) return;
        DevicesPage.Visibility = target == "Devices" ? Visibility.Visible : Visibility.Collapsed;
        RulesPage.Visibility = target == "Rules" ? Visibility.Visible : Visibility.Collapsed;
        EventsPage.Visibility = target == "Events" ? Visibility.Visible : Visibility.Collapsed;
        foreach (var button in new[] { DevicesNav, RulesNav, EventsNav })
            button.Background = new System.Windows.Media.SolidColorBrush(
                button.Tag?.ToString() == target ? System.Windows.Media.Color.FromRgb(234, 241, 250) : System.Windows.Media.Colors.Transparent);
        (PageTitle.Text, PageSubtitle.Text) = target switch
        {
            "Rules" => ("解锁设置", "设置允许解锁的条件和确认动作"),
            "Events" => ("运行记录", "查看后台运行和靠近状态变化"),
            _ => ("设备", "选择用于靠近解锁的手机")
        };
    }

    private int SelectedMode => BothMode.IsChecked == true ? 2 : LanMode.IsChecked == true ? 1 : 0;

    private void OnModeChanged(object sender, RoutedEventArgs e)
    {
        // Checked can fire during XAML construction, before LanFields exists.
        if (LanFields != null)
            LanFields.Visibility = SelectedMode == 0 ? Visibility.Collapsed : Visibility.Visible;
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
            UnlockKeyBox.Text = settings.UnlockKey == 13 ? "Enter" : ((char)settings.UnlockKey).ToString();
            AutoLockCheck.IsChecked = settings.AutomaticLock;
            BluetoothMode.IsChecked = settings.UnlockMode == 0;
            LanMode.IsChecked = settings.UnlockMode == 1;
            BothMode.IsChecked = settings.UnlockMode == 2;
            LanIpBox.Text = settings.LanIp;
            LanMacBox.Text = settings.LanMac;
            StartupCheck.IsChecked = _backend.StartupEnabled();
            DelayBox.SelectedItem = DelayBox.Items.OfType<ComboBoxItem>()
                .FirstOrDefault(item => item.Tag?.ToString() == settings.LockDelaySeconds.ToString())
                ?? DelayBox.Items[1];
            RefreshStatus();
            _refreshTimer.Tick += (_, _) => RefreshStatus();
            _refreshTimer.Start();
        }
        catch (Exception error) { ShowError(error); }
    }

    private void RefreshStatus()
    {
        try
        {
            var selectedAddress = (NearbyList.SelectedItem as DeviceRow)?.Address;
            var discovered = _backend.ReadDiscovered();
            var selectedDeviceAddress = (SelectedList.SelectedItem as DeviceRow)?.Address;
            // Keep each device in one list and preserve selection across snapshot refreshes.
            var savedDevices = discovered.Where(item => item.IsSelected).ToArray();
            SavedDevicesText.Text = savedDevices.Length == 0 ? "尚未添加手机，请从左侧选择。" :
                $"已添加 {savedDevices.Length} 台；添加与移除后立即保存。";
            NearbyList.ItemsSource = discovered.Where(item => !item.IsSelected).ToArray();
            NearbyList.SelectedItem = discovered.FirstOrDefault(item => !item.IsSelected && item.Address == selectedAddress);
            SelectedList.ItemsSource = savedDevices;
            SelectedList.SelectedItem = savedDevices.FirstOrDefault(item => item.Address == selectedDeviceAddress);
            var status = _backend.ReadStatus();
            var online = _backend.IsRunning;
            var detected = status.GetValueOrDefault("detected", "0");
            var selected = status.GetValueOrDefault("selected", "0");
            var signal = status.GetValueOrDefault("mean", "0");
            var withinRange = status.GetValueOrDefault("near", "0") == "1";
            StatusText.Text = !online ? "后台未运行" : withinRange ? "解锁条件已满足" : "等待解锁条件";
            MeanText.Text = detected == "0" ? "-- dBm" : $"{signal} dBm";
            DeviceCountText.Text = $"参与平均 {detected} / 已选择 {selected} 台";
            // Compare against the backend's saved threshold, not the unsaved slider value.
            var threshold = status.GetValueOrDefault("unlock_threshold",
                _backend.ReadSettings().UnlockThreshold.ToString());
            var bluetoothNear = status.GetValueOrDefault("bluetooth_near", "0") == "1";
            BluetoothStateText.Text = detected == "0" ? "蓝牙：无近期信号" :
                $"蓝牙平均 {signal} dBm {(bluetoothNear ? "≥" : "<")} {threshold} dBm\n{(bluetoothNear ? "已达到解锁阈值" : "未达到解锁阈值")}";
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
        if (device.MissingFromScan)
        {
            HintText.Text = "该设备当前没有实时广播，请等它出现信号后再加入。";
            return;
        }
        try
        {
            await _backend.RunCommandAsync("--add", device.Address, device.Name);
            RefreshStatus();
            HintText.Text = "设备已添加并保存。请前往解锁设置选择规则。";
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
            RefreshStatus();
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
        var mode = SelectedMode;
        var keyText = UnlockKeyBox.Text.Trim().ToUpperInvariant();
        if (keyText.Length != 0 && keyText != "ENTER" &&
            (keyText.Length != 1 || keyText[0] < 'A' || keyText[0] > 'Z'))
        {
            HintText.Text = "键盘解锁键只能填写 Enter 或单个英文字母 A–Z。";
            UnlockKeyBox.Focus();
            return;
        }
        var unlockKey = keyText.Length == 1 ? ((int)keyText[0]).ToString() : "13";
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
            await _backend.RunCommandAsync("--configure-v3", unlock.ToString(), lockAt.ToString(),
                delay, AutoLockCheck.IsChecked == true ? "1" : "0",
                AutoUnlockCheck.IsChecked == true ? "1" : "0", mode.ToString(), ip, mac, unlockKey);
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
        System.Windows.MessageBox.Show(this, error.Message, "蓝牙靠近解锁", MessageBoxButton.OK,
            MessageBoxImage.Warning);
    }
}
