using System;
using System.Collections.Generic;

namespace ECS3DLauncher.Services;

// What the launcher does with an exception nothing caught: write it down and, on the UI thread, tell the user.
public sealed class UnhandledErrorPolicy
{
    public const string ToastMessage = "Something went wrong - see launcher.log";

    public const int StormLimit = 5;

    public static readonly TimeSpan StormWindow = TimeSpan.FromSeconds(10);

    private readonly ILauncherLog _log;
    private readonly string _logPath;
    private readonly Func<DateTimeOffset> _now;
    private readonly Queue<DateTimeOffset> _recent = new();
    private bool _notifying;

    public UnhandledErrorPolicy(ILauncherLog log, string logPath)
        : this(log, logPath, () => DateTimeOffset.UtcNow)
    {
    }

    public UnhandledErrorPolicy(ILauncherLog log, string logPath, Func<DateTimeOffset> now)
    {
        _log = log;
        _logPath = logPath;
        _now = now;
    }

    // Null until the window exists; an error with no one to tell is left to end the process.
    public Action<LaunchNotice>? ShowNotice { get; set; }

    // Returns whether the exception was handled, i.e. whether the launcher keeps running.
    public bool HandleUiException(Exception exception)
    {
        LogError($"Unhandled exception on the UI thread: {exception}");
        var show = ShowNotice;
        if (exception is OutOfMemoryException || show is null)
        {
            return false;
        }

        // An exception raised while the notice is being shown must not ask for another notice.
        if (_notifying)
        {
            return true;
        }

        if (IsStorm())
        {
            LogError("Too many unhandled exceptions in a row; the launcher is closing.");
            return false;
        }

        _notifying = true;
        try
        {
            show(new LaunchNotice(ToastMessage, _logPath));
        }
        catch (Exception failure)
        {
            // Surviving matters more than the toast.
            LogError($"Could not show the error notice: {failure}");
        }
        finally
        {
            _notifying = false;
        }

        return true;
    }

    public void LogBackgroundException(string source, object? error)
    {
        LogError($"Unhandled exception ({source}): {error ?? "no exception object"}");
    }

    // A fault that throws on every dispatcher job would otherwise spin and flood the log.
    private bool IsStorm()
    {
        var now = _now();
        while (_recent.Count > 0 && now - _recent.Peek() > StormWindow)
        {
            _recent.Dequeue();
        }

        if (_recent.Count >= StormLimit)
        {
            return true;
        }

        _recent.Enqueue(now);
        return false;
    }

    private void LogError(string message)
    {
        try
        {
            _log.Error(message);
        }
        catch (Exception)
        {
            // Nowhere left to report a failing log; the handler must not raise a second exception.
        }
    }
}
