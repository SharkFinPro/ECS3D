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
    private readonly RunningEditors? _editors;

    // When editors is given, a project's editor stays tracked until its process exits.
    public LaunchCoordinator(EngineLauncher launcher, EarlyExitWatcher watcher, Action<LaunchNotice> notify)
        : this(launcher, watcher, notify, null)
    {
    }

    public LaunchCoordinator(
        EngineLauncher launcher,
        EarlyExitWatcher watcher,
        Action<LaunchNotice> notify,
        RunningEditors? editors)
    {
        _launcher = launcher;
        _watcher = watcher;
        _notify = notify;
        _editors = editors;
    }

    // The returned task ends when the watch window does; callers that do not care may drop it.
    public Task OpenProjectAsync(string projectFolder, LaunchOptions options) =>
        ReportAsync(() => _launcher.OpenProject(projectFolder, options), EngineExecutable.Editor, projectFolder);

    public Task ExploreEngineAsync(LaunchOptions options) =>
        ReportAsync(() => _launcher.ExploreEngine(options), EngineExecutable.Editor, null);

    public Task PlayProjectAsync(string projectFolder) =>
        ReportAsync(() => _launcher.PlayProject(projectFolder), EngineExecutable.Client, null);

    public Task JoinGameAsync(string address, int port) =>
        ReportAsync(() => _launcher.JoinGame(address, port), EngineExecutable.Client, null);

    // Callers drop the task, so nothing the start or the watch throws may escape it.
    private async Task ReportAsync(Func<LaunchResult> start, EngineExecutable executable, string? trackedProject)
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

        // Disposing releases the launcher's handle only; the process keeps running. A tracked editor's
        // handle is released by the tracker once the process exits.
        var process = result.Process;
        var tracked = _editors is not null && trackedProject is not null;
        var watchEnded = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        if (tracked)
        {
            _ = TrackAsync(process, trackedProject!, watchEnded.Task);
        }

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
        finally
        {
            watchEnded.SetResult();
            if (!tracked)
            {
                process.Dispose();
            }
        }

        if (notice is not null)
        {
            _notify(notice);
        }
    }

    // Never kills the process; a wait that fails counts as the editor being gone. The handle is kept until the
    // early-exit watch has finished with it.
    private async Task TrackAsync(IRunningProcess process, string projectPath, Task watchEnded)
    {
        try
        {
            _editors!.Started(projectPath);
            await process.WaitForExitAsync(CancellationToken.None);
        }
        catch (Exception exception) when (IsLaunchFailure(exception))
        {
            // The handle is unusable, so there is nothing left to wait on.
        }
        finally
        {
            await watchEnded;
            process.Dispose();
            _editors!.Stopped(projectPath);
        }
    }

    private static bool IsLaunchFailure(Exception exception) =>
        exception is InvalidOperationException or Win32Exception or ArgumentException
            or PlatformNotSupportedException or IOException or ProcessStartException;
}
