using System;
using System.Threading;
using System.Threading.Tasks;

namespace ECS3DLauncher.Services;

public sealed class EarlyExitWatcher
{
    private static readonly TimeSpan DefaultWindow = TimeSpan.FromSeconds(10);

    private readonly IClock _clock;
    private readonly UserDataDirectory _userData;
    private readonly TimeSpan _window;

    public EarlyExitWatcher(IClock clock, UserDataDirectory userData)
        : this(clock, userData, DefaultWindow)
    {
    }

    public EarlyExitWatcher(IClock clock, UserDataDirectory userData, TimeSpan window)
    {
        _clock = clock;
        _userData = userData;
        _window = window;
    }

    // Returns null for a clean exit, a process that outlives the window, or a cancelled watch.
    public async Task<EarlyExit?> WatchAsync(
        IRunningProcess process,
        EngineExecutable executable,
        CancellationToken cancellationToken)
    {
        var started = _clock.UtcNow;
        using var windowEnd = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        windowEnd.CancelAfter(_window);

        int exitCode;
        try
        {
            exitCode = await process.WaitForExitAsync(windowEnd.Token);
        }
        catch (OperationCanceledException)
        {
            return null;
        }

        if (exitCode == 0 || _clock.UtcNow - started > _window)
        {
            return null;
        }

        return new EarlyExit(executable, exitCode, _userData.LogFile(executable));
    }
}
