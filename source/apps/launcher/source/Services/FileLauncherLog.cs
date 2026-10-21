using System;
using System.Globalization;
using System.IO;

namespace ECS3DLauncher.Services;

public sealed class FileLauncherLog : ILauncherLog
{
    private readonly string _path;
    private readonly Func<DateTimeOffset> _now;
    private readonly object _lock = new();
    private bool _started;

    public FileLauncherLog(UserDataDirectory userData)
        : this(userData.LauncherLogFile(), () => DateTimeOffset.UtcNow)
    {
    }

    public FileLauncherLog(string path, Func<DateTimeOffset> now)
    {
        _path = path;
        _now = now;
    }

    public void Warn(string message)
    {
        var stamp = _now().UtcDateTime.ToString("yyyy-MM-dd'T'HH:mm:ss.fff'Z'", CultureInfo.InvariantCulture);
        var line = $"{stamp} [warn] {message}{Environment.NewLine}";
        lock (_lock)
        {
            try
            {
                Write(line);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or NotSupportedException
                                           or ArgumentException)
            {
                // Logging must never take the launcher down; the line is dropped and the next write retries.
                return;
            }
        }
    }

    // The first write of a run truncates the file, like the engine's FileSink.
    private void Write(string line)
    {
        var directory = Path.GetDirectoryName(_path);
        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }

        if (_started)
        {
            File.AppendAllText(_path, line);
            return;
        }

        File.WriteAllText(_path, line);
        _started = true;
    }
}
