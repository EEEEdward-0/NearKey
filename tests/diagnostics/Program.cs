using BluetoothUnlock.UI;
using System.IO.Compression;

var directory = Path.Combine(Path.GetTempPath(), "NearKeyDiagnosticsTest-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(directory);
try
{
    var log = Path.Combine(directory, "install.log");
    File.WriteAllText(log, "service_install_failed: test");
    File.WriteAllText(Path.Combine(directory, "settings.ini"), "private test fixture");
    var destination = Path.Combine(directory, "diagnostics.zip");

    var count = Diagnostics.Export(destination, [("install.log", log)]);
    using var archive = ZipFile.OpenRead(destination);
    var names = archive.Entries.Select(entry => entry.FullName).ToArray();
    if (count != 1 || !names.Contains("install.log") || !names.Contains("README.txt") || names.Contains("settings.ini"))
        throw new Exception("Diagnostic archive contents are incorrect.");
    using var reader = new StreamReader(archive.GetEntry("install.log")!.Open());
    if (reader.ReadToEnd() != "service_install_failed: test")
        throw new Exception("Diagnostic log content was changed.");
    Console.WriteLine("Diagnostic export passed.");
}
finally
{
    var tempRoot = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
    var testPath = Path.GetFullPath(directory);
    if (!testPath.StartsWith(tempRoot, StringComparison.OrdinalIgnoreCase))
        throw new Exception("Test directory escaped the temporary folder.");
    Directory.Delete(testPath, true);
}
