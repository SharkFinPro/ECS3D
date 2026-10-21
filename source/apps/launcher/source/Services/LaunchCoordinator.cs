using System;
using System.ComponentModel;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace ECS3DLauncher.Services;

// Starts engine processes and reports a failed start or an early exit through the notify callback.
// The processes are independent: nothing here waits on one past the watch window or kills one.
public sealed class LaunchCoordinator
{
    private readonly EngineLauncher _launcher;
    private readonly EarlyExitWatcher _watcher;
    private readonly Action<LaunchNotice> _notify;

    public LaunchCoordinator(EngineLauncher launcher, EarlyExitWatcher watcher, Action<LaunchNotice> notify)
    {
        _launcher = launcher;
        _watcher = watcher;
        _notify = notify;
    }

    // The returned task ends when the watch window does; callers that do not care may drop it.
    public Task OpenProjectAsync(string projectFolder, LaunchOptions options) =>
        ReportAsync(() => _launcher.OpenProject(projectFolder, options), EngineExecutable.Editor);

    public Task ExploreEngineAsync(LaunchOptions options) =>
        ReportAsync(() => _launcher.ExploreEngine(options), EngineExecutable.Editor);

    // Callers drop the task, so nothing the start or the watch throws may escape it.
    private async Task ReportAsync(Func<LaunchResult> start, EngineExecutable executable)
    {
        LaunchResult result;
        try
        {
            result = start();
        }
        catch (Exception exception) when (IsLaunchFailure(exception))
        {
            _notify(EarlyExitNotice.Failure(exception.Message));
            return;
        }

        if (result.Process is null)
        {
            _notify(EarlyExitNotice.Failure(result.Error ?? "The process could not be started."));
            return;
        }

        // Disposing releases the launcher's handle only; the process keeps running.
        using var process = result.Process;
        LaunchNotice? notice;
        try
        {
            var outcome = await _watcher.WatchAsync(process, executable, CancellationToken.None);
            notice = EarlyExitNotice.From(outcome);
        }
        catch (Exception exception) when (IsLaunchFailure(exception))
        {
            notice = EarlyExitNotice.Failure(exception.Message);
        }

        if (notice is not null)
        {
            _notify(notice);
        }
    }

    private static bool IsLaunchFailure(Exception exception) =>
        exception is InvalidOperationException or Win32Exception or ArgumentException
            or PlatformNotSupportedException or IOException or ProcessStartException;
}
