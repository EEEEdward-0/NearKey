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
    private readonly System.IO.Stream _iconStream;
    private bool _exitRequested;

    public MainWindow()
    {
        InitializeComponent();
        _iconStream = System.Windows.Application.GetResourceStream(new Uri("pack://application:,,,/Assets/App.ico")).Stream;
        _trayIcon = new Forms.NotifyIcon
        {
            Icon = new System.Drawing.Icon(_iconStream),
            Text = "近钥 NearKey",
            Visible = true,
            ContextMenuStrip = new Forms.ContextMenuStrip()
        };
        _trayIcon.ContextMenuStrip.Items.Add("打开设置", null, (_, _) => Dispatcher.Invoke(ShowFromTray));
        _trayIcon.ContextMenuStrip.Items.Add("立即锁定", null, (_, _) => Dispatcher.Invoke(() => OnManualLock(this, new RoutedEventArgs())));
        _trayIcon.ContextMenuStrip.Items.Add("退出设置界面", null, (_, _) => Dispatcher.Invoke(() => { _exitRequested = true; Close(); }));
        _trayIcon.DoubleClick += (_, _) => Dispatcher.Invoke(ShowFromTray);
        StateChanged += (_, _) => { if (WindowState == WindowState.Minimized) Hide(); };
        Closing += (_, eventArgs) =>
        {
            // Closing the settings window keeps its tray entry available; Exit is explicit.
            if (_exitRequested) return;
            eventArgs.Cancel = true;
            Hide();
        };
        Loaded += OnLoaded;
        Closed += (_, _) => { _refreshTimer.Stop(); _trayIcon.Visible = false; _trayIcon.Icon.Dispose(); _trayIcon.Dispose(); _iconStream.Dispose(); };
        UnlockSlider.ValueChanged += (_, _) => UnlockValue.Text = $"{UnlockSlider.Value:0} dBm";
        LockSlider.ValueChanged += (_, _) => LockValue.Text = $"{LockSlider.Value:0} dBm";
    }

    internal void ShowFromTray()
    {
        Show();
        WindowState = WindowState.Normal;
        Activate();
    }

    private async void OnPreview(object sender, RoutedEventArgs e)
    {
        if (!int.TryParse(PreviewRssiBox.Text, out var rssi) || rssi is < -127 or > 20 ||
            !int.TryParse(PreviewLanCountBox.Text, out var online) || online < 0)
        {
            PreviewResult.Text = "请输入 -127 至 20 dBm 的信号值，以及非负的在线台数。";
            return;
        }
        try
        {
            var output = await _backend.RunCommandAsync("--preview", rssi.ToString(), online.ToString());
            var result = output.Split('\n', StringSplitOptions.RemoveEmptyEntries)
                .Select(line => line.Trim().Split('=', 2))
                .Where(parts => parts.Length == 2)
                .ToDictionary(parts => parts[0], parts => parts[1]);
            var settings = _backend.ReadSettings();
            var bluetooth = result.GetValueOrDefault("bluetooth") == "1";
            var lan = result.GetValueOrDefault("lan") == "1";
            var eligible = result.GetValueOrDefault("unlock") == "1";
            var lockCounting = result.GetValueOrDefault("lock_counting") == "1";
            var reasons = new List<string>();
            if (!settings.AutomaticUnlock) reasons.Add("靠近解锁已关闭");
            if (settings.UnlockMode != 1 && !bluetooth) reasons.Add("蓝牙信号未达到解锁阈值");
            if (settings.UnlockMode != 0 && !lan) reasons.Add("局域网在线设备不足");
            PreviewResult.Text = $"预演结果：{(eligible ? "满足解锁条件" : "不能解锁")}。" +
                (reasons.Count > 0 ? $"原因：{string.Join("；", reasons)}。" : "所需条件均通过。") +
                $" 自动锁定：{(lockCounting ? "若持续离开将开始倒计时" : "不会开始离开倒计时")}。";
        }
        catch (Exception error) { PreviewResult.Text = $"预演失败：{error.Message}"; }
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
            LanIpBox.Text = settings.LanDevices.ElementAtOrDefault(0)?.Ip ?? "";
            LanMacBox.Text = settings.LanDevices.ElementAtOrDefault(0)?.Mac ?? "";
            LanIpBox2.Text = settings.LanDevices.ElementAtOrDefault(1)?.Ip ?? "";
            LanMacBox2.Text = settings.LanDevices.ElementAtOrDefault(1)?.Mac ?? "";
            StartupCheck.IsChecked = _backend.StartupEnabled();
            StartupCheck.IsEnabled = !_backend.ServiceInstalled;
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
                    ? $"局域网：在线 {status.GetValueOrDefault("lan_online", "0")} / 已配置 {status.GetValueOrDefault("lan_selected", "0")} 台，全部通过"
                    : $"局域网：在线 {status.GetValueOrDefault("lan_online", "0")} / 已配置 {status.GetValueOrDefault("lan_selected", "0")} 台，尚未全部通过";
            var lanDetails = new List<string>();
            var savedLan = _backend.ReadSettings().LanDevices;
            for (var i = 0; i < savedLan.Count; i++)
            {
                var prefix = $"lan_device{i}";
                if (!status.GetValueOrDefault($"{prefix}_mac", "").Equals(savedLan[i].Mac, StringComparison.OrdinalIgnoreCase))
                    continue;
                var currentIp = status.GetValueOrDefault($"{prefix}_ip", savedLan[i].Ip);
                var seen = long.TryParse(status.GetValueOrDefault($"{prefix}_last_seen", "0"), out var seconds) && seconds > 0
                    ? DateTimeOffset.FromUnixTimeSeconds(seconds).ToLocalTime().ToString("MM-dd HH:mm:ss") : "尚未检测到";
                lanDetails.Add($"设备 {i + 1}：保存 IP {savedLan[i].Ip}；当前 IP {currentIp}；" +
                    $"{(status.GetValueOrDefault($"{prefix}_online", "0") == "1" ? "在线" : "离线")}；最近在线 {seen}");
            }
            LanStateText.ToolTip = string.Join("\n", lanDetails);
            EventList.ItemsSource = _backend.ReadEvents().Select(TranslateEvent).ToArray();
        }
        catch (Exception error) { HintText.Text = $"读取状态失败：{error.Message}"; }
    }

    private static string TranslateEvent(string line) => line
        .Replace("service_monitor_started", "开机服务已启动")
        .Replace("bluetooth_waiting", "等待蓝牙就绪")
        .Replace("monitor_failed", "后台异常停止，请检查服务")
        .Replace("monitor_started", "后台已启动")
        .Replace("monitor_stopped", "后台已停止")
        .Replace("unlock_range_entered", "进入解锁范围")
        .Replace("unlock_range_left", "离开解锁范围")
        .Replace("automatic_lock", "已自动锁定")
        .Replace("lan_ip_recovered", "已按 Wi-Fi MAC 找回当前 IP");

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
        var ip2 = LanIpBox2.Text.Trim();
        var mac2 = LanMacBox2.Text.Trim().Replace(":", "").Replace("-", "").ToUpperInvariant();
        bool Complete(string addressText, string macText) =>
            IPAddress.TryParse(addressText, out var address) && address.AddressFamily == AddressFamily.InterNetwork &&
            macText.Length == 12 && macText.All(Uri.IsHexDigit) && macText != "000000000000";
        if (((mode != 0 || ip.Length > 0 || mac.Length > 0) && !Complete(ip, mac)) ||
            ((ip2.Length > 0 || mac2.Length > 0) && (!Complete(ip2, mac2) || !Complete(ip, mac))))
        {
            HintText.Text = "未保存：每台局域网设备需完整填写 IP 和 Wi-Fi MAC。第二台请补全两项，或全部清空；添加蓝牙设备不会自动加入局域网配置。";
            return;
        }
        if (ip2.Length > 0 && mac == mac2)
        {
            HintText.Text = "两台设备的 Wi-Fi MAC 不能重复。";
            return;
        }
        var saveButton = sender as System.Windows.Controls.Button;
        if (saveButton != null) saveButton.IsEnabled = false;
        LanFields.IsEnabled = false;
        try
        {
            HintText.Text = "正在校验每台设备的 IP/MAC，请保持手机亮屏并连接同一 Wi-Fi…";
            var verifiedIp = ip.Length == 0 ? "" : await VerifyLanAddress(ip, mac, 1);
            var verifiedIp2 = ip2.Length == 0 ? "" : await VerifyLanAddress(ip2, mac2, 2);
            if (verifiedIp == null || verifiedIp2 == null) return;
            LanIpBox.Text = verifiedIp;
            LanIpBox2.Text = verifiedIp2;
            if (verifiedIp2.Length > 0 && verifiedIp == verifiedIp2)
                throw new InvalidOperationException("校验后的两台设备 IP 重复，设置未保存。");
            if ((verifiedIp != ip || verifiedIp2 != ip2) &&
                System.Windows.MessageBox.Show(this,
                    $"IP 已修正：\n设备 1：{ip} → {verifiedIp}\n设备 2：{ip2} → {verifiedIp2}\n确认使用修正后的地址并保存？",
                    "确认 IP 修正", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes)
            {
                HintText.Text = "已填入修正地址，尚未保存。";
                return;
            }
            await _backend.RunCommandAsync("--configure-v4", unlock.ToString(), lockAt.ToString(),
                delay, AutoLockCheck.IsChecked == true ? "1" : "0",
                AutoUnlockCheck.IsChecked == true ? "1" : "0", mode.ToString(), verifiedIp, mac, unlockKey, verifiedIp2, mac2);
            await _backend.RunCommandAsync("--startup", StartupCheck.IsChecked == true ? "1" : "0");
            HintText.Text = "设置已保存，后台会自动读取新规则。";
        }
        catch (Exception error) { ShowError(error); }
        finally
        {
            LanFields.IsEnabled = true;
            if (saveButton != null) saveButton.IsEnabled = true;
        }
    }

    private async Task<string?> VerifyLanAddress(string ip, string mac, int number)
    {
        try
        {
            var result = (await _backend.RunCommandAsync("--resolve-lan", ip, mac)).Trim().Split('\t');
            if (result.Length != 2 || !result[1].Equals(mac, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("校验结果不匹配。");
            return result[0];
        }
        catch (Exception)
        {
            HintText.Text = $"设备 {number} 未响应或 IP/MAC 不匹配，未保存。请唤醒手机，核对 Wi-Fi MAC 后重试。";
            return null;
        }
    }

    private async void OnResolveLan(object sender, RoutedEventArgs e)
    {
        var second = ReferenceEquals(sender, ResolveLanButton2);
        var ipBox = second ? LanIpBox2 : LanIpBox;
        var macBox = second ? LanMacBox2 : LanMacBox;
        var button = second ? ResolveLanButton2 : ResolveLanButton;
        var ip = ipBox.Text.Trim();
        var mac = macBox.Text.Trim().Replace(":", "").Replace("-", "").ToUpperInvariant();
        if ((ip.Length == 0 && mac.Length == 0) ||
            (ip.Length > 0 && (!IPAddress.TryParse(ip, out var address) ||
                address.AddressFamily != AddressFamily.InterNetwork)) ||
            (mac.Length > 0 && (mac.Length != 12 || !mac.All(Uri.IsHexDigit))))
        {
            HintText.Text = "请先填写有效的 IPv4 地址或 12 位 Wi-Fi MAC 地址。";
            return;
        }
        button.IsEnabled = false;
        HintText.Text = "正在探测同网段设备，约需 20 秒；请保持手机 Wi-Fi 开启…";
        try
        {
            var result = (await _backend.RunCommandAsync("--resolve-lan", ip, mac)).Trim().Split('\t');
            if (result.Length != 2) throw new InvalidOperationException("无法读取识别结果。");
            if (mac.Length == 0 && System.Windows.MessageBox.Show(this,
                $"该 IP 对应的设备信息：\nIP：{result[0]}\nWi-Fi MAC：{result[1]}\n请与手机当前 Wi-Fi 详情核对。确认这是你的手机吗？",
                "确认设备身份", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes)
            {
                HintText.Text = "尚未确认设备身份，未填入补全结果。";
                return;
            }
            ipBox.Text = result[0];
            macBox.Text = result[1];
            HintText.Text = "已补全。请确认设备信息，然后点击保存设置。";
        }
        catch (Exception)
        {
            HintText.Text = "未找到匹配设备。请将手机连接到同一 Wi-Fi 并唤醒，再重试；也可手动填写。";
        }
        finally { button.IsEnabled = true; }
    }

    private async void OnManualLock(object sender, RoutedEventArgs e)
    {
        try { await _backend.RunCommandAsync("--lock"); }
        catch (Exception error) { ShowError(error); }
    }

    private void ShowError(Exception error)
    {
        HintText.Text = error.Message;
        System.Windows.MessageBox.Show(this, error.Message, "近钥 NearKey", MessageBoxButton.OK,
            MessageBoxImage.Warning);
    }
}
