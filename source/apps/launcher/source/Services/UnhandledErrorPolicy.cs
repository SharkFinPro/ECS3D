using System;

namespace ECS3DLauncher.Services;

// What the launcher does with an exception nothing caught: write it down and, on the UI thread, tell the user.
public sealed class UnhandledErrorPolicy
{
    public const string ToastMessage = "Something went wrong - see launcher.log";

    private readonly ILauncherLog _log;
    private readonly string _logPath;
    private bool _notifying;

    public UnhandledErrorPolicy(ILauncherLog log, string logPath)
    {
        _log = log;
        _logPath = logPath;
    }

    // Null until the window exists; an error with no one to tell is left to end the process.
    public Action<LaunchNotice>? ShowNotice { get; set; }

    // Returns whether the exception was handled, i.e. whether the launcher keeps running.
    public bool HandleUiException(Exception exception)
    {
        _log.Error($"Unhandled exception on the UI thread: {exception}");
        var show = ShowNotice;
        if (show is null)
        {
            return false;
        }

        // An exception raised while the notice is being shown must not ask for another notice.
        if (_notifying)
        {
            return true;
        }

        _notifying = true;
        try
        {
            show(new LaunchNotice(ToastMessage, _logPath));
        }
        catch (Exception failure)
        {
            // Surviving matters more than the toast.
            _log.Error($"Could not show the error notice: {failure}");
        }
        finally
        {
            _notifying = false;
        }

        return true;
    }

    public void LogBackgroundException(string source, object? error)
    {
        _log.Error($"Unhandled exception ({source}): {error ?? "no exception object"}");
    }
}
