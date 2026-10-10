using System.Windows;
using System.Threading;

namespace BluetoothUnlock.UI;

public partial class App : System.Windows.Application
{
    private Mutex? _instanceMutex;
    private EventWaitHandle? _showSignal;

    protected override void OnStartup(StartupEventArgs e)
    {
        var trayOnly = e.Args.Contains("--tray", StringComparer.OrdinalIgnoreCase);
        var sid = System.Security.Principal.WindowsIdentity.GetCurrent().User?.Value;
        var name = $"Local\\NearKey.Settings.{sid}";
        _instanceMutex = new Mutex(true, name, out var firstInstance);
        if (!firstInstance)
        {
            // A shortcut or logon launch should reopen the existing tray window.
            using var signal = new EventWaitHandle(false, EventResetMode.AutoReset, name + ".Show");
            if (!trayOnly) signal.Set();
            _instanceMutex.Dispose();
            _instanceMutex = null;
            Shutdown();
            return;
        }
        var showSignal = new EventWaitHandle(false, EventResetMode.AutoReset, name + ".Show");
        _showSignal = showSignal;
        base.OnStartup(e);
        try { MainWindow = new MainWindow(); }
        catch (Exception error) { Diagnostics.Record("startup", error); throw; }
        if (!trayOnly) MainWindow.Show();
        _ = Task.Run(() =>
        {
            try
            {
                while (showSignal.WaitOne())
                    Dispatcher.BeginInvoke(() => (this.MainWindow as MainWindow)?.ShowFromTray());
            }
            catch (ObjectDisposedException) { /* The settings process is exiting. */ }
        });
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _showSignal?.Dispose();
        if (_instanceMutex != null)
        {
            _instanceMutex.ReleaseMutex();
            _instanceMutex.Dispose();
        }
        base.OnExit(e);
    }
}
