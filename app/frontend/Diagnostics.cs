using System.IO;
using System.IO.Compression;
using System.Text;

namespace BluetoothUnlock.UI;

internal static class Diagnostics
{
    private static readonly string UiLog = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "BluetoothUnlock", "ui-errors.log");

    internal static void Record(string operation, Exception error)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(UiLog)!);
            if (File.Exists(UiLog) && new FileInfo(UiLog).Length > 1024 * 1024)
                File.Move(UiLog, Path.ChangeExtension(UiLog, ".previous.log"), true);
            var message = error.Message.Replace('\r', ' ').Replace('\n', ' ');
            File.AppendAllText(UiLog,
                $"{DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss zzz}\t{operation}\t{error.GetType().Name}: {message}{Environment.NewLine}",
                Encoding.UTF8);
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    internal static int Export(string destination, IEnumerable<(string Name, string Path)> files)
    {
        using var archive = new ZipArchive(File.Create(destination), ZipArchiveMode.Create);
        var count = 0;
        foreach (var (name, path) in files.Append(("ui-errors.log", UiLog)))
        {
            if (!File.Exists(path)) continue;
            using var source = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            using var entry = archive.CreateEntry(name).Open();
            source.CopyTo(entry);
            count++;
        }
        using (var entry = new StreamWriter(archive.CreateEntry("README.txt").Open(), Encoding.UTF8))
            entry.WriteLine($"NearKey diagnostics exported {DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss zzz}. Settings and stored credentials are excluded. Review log contents before sharing.");
        return count;
    }
}
