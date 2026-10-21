using System;
using System.Threading;
using System.Threading.Tasks;

namespace ECS3DLauncher.Services;

public sealed class EarlyExitWatcher
{
    private static readonly TimeSpan DefaultWindow = TimeSpan.FromSeconds(10);

    private readonly UserDataDirectory _userData;
    private readonly TimeSpan _window;

    public EarlyExitWatcher(UserDataDirectory userData)
        : this(userData, DefaultWindow)
    {
    }

    public EarlyExitWatcher(UserDataDirectory userData, TimeSpan window)
    {
        _userData = userData;
        _window = window;
    }

    // A caller that kills the process itself must cancel the watch first, or the kill reads as an early exit.
    public async Task<EarlyExitResult> WatchAsync(
        IRunningProcess process,
        EngineExecutable executable,
        CancellationToken cancellationToken)
    {
        using var windowEnd = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        windowEnd.CancelAfter(_window);

        int exitCode;
        try
        {
            exitCode = await process.WaitForExitAsync(windowEnd.Token);
        }
        catch (OperationCanceledException)
        {
            var kind = cancellationToken.IsCancellationRequested
                ? EarlyExitKind.Cancelled
                : EarlyExitKind.StillRunning;
            return new EarlyExitResult(kind, executable, null, null);
        }

        if (exitCode == 0)
        {
            return new EarlyExitResult(EarlyExitKind.ExitedCleanly, executable, 0, null);
        }

        return new EarlyExitResult(EarlyExitKind.ExitedEarly, executable, exitCode, _userData.LogFile(executable));
    }
}
