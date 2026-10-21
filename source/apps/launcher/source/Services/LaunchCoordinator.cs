using System;
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
        ReportAsync(_launcher.OpenProject(projectFolder, options), EngineExecutable.Editor);

    public Task ExploreEngineAsync(LaunchOptions options) =>
        ReportAsync(_launcher.ExploreEngine(options), EngineExecutable.Editor);

    private async Task ReportAsync(LaunchResult result, EngineExecutable executable)
    {
        if (result.Process is null)
        {
            _notify(EarlyExitNotice.Failure(result.Error ?? "The process could not be started."));
            return;
        }

        // Disposing releases the launcher's handle only; the process keeps running.
        using var process = result.Process;
        var outcome = await _watcher.WatchAsync(process, executable, CancellationToken.None);
        var notice = EarlyExitNotice.From(outcome);
        if (notice is not null)
        {
            _notify(notice);
        }
    }
}
