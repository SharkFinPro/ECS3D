using System;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;

namespace ECS3DLauncher.Services;

// One hosted server at a time, owned by the launcher. The UI calls it from its own thread; Ended is raised
// from a thread-pool continuation, so a handler must marshal to the UI thread itself. Nothing stops the
// server on its own (not even disposal): quitting while hosting is the UI's prompt, so it reads IsRunning
// and IsStarting.
public sealed class HostSession
{
    private const string LocalHost = "127.0.0.1";

    private static readonly TimeSpan DefaultWindow = TimeSpan.FromSeconds(10);
    private static readonly TimeSpan DefaultPollInterval = TimeSpan.FromMilliseconds(200);

    private readonly EngineLauncher _launcher;
    private readonly IPortProbe _probe;
    private readonly UserDataDirectory _userData;
    private readonly TimeProvider _clock;
    private readonly TimeSpan _window;
    private readonly TimeSpan _pollInterval;
    private readonly Func<TimeSpan, CancellationToken, Task> _delay;
    private readonly object _gate = new();

    private ActiveSession? _active;
    private CancellationTokenSource? _pending;

    public HostSession(EngineLauncher launcher, IPortProbe probe, UserDataDirectory userData)
        : this(launcher, probe, userData, TimeProvider.System, DefaultWindow, DefaultPollInterval, Task.Delay)
    {
    }

    public HostSession(
        EngineLauncher launcher,
        IPortProbe probe,
        UserDataDirectory userData,
        TimeProvider clock,
        TimeSpan window,
        TimeSpan pollInterval,
        Func<TimeSpan, CancellationToken, Task> delay)
    {
        _launcher = launcher;
        _probe = probe;
        _userData = userData;
        _clock = clock;
        _window = window;
        _pollInterval = pollInterval;
        _delay = delay;
    }

    // Raised when the server exits on its own, with any exit code. Not raised by Stop. Handlers must not
    // throw: the exception would fault an unobserved task and skip later handlers.
    public event Action<HostSessionEnd>? Ended;

    // True while StartAsync is waiting for the server; Stop cancels it.
    public bool IsStarting
    {
        get
        {
            lock (_gate)
            {
                return _pending is not null;
            }
        }
    }

    public bool IsRunning
    {
        get
        {
            lock (_gate)
            {
                return _active is not null;
            }
        }
    }

    public int? Port
    {
        get
        {
            lock (_gate)
            {
                return _active?.Port;
            }
        }
    }

    public string? DisplayName
    {
        get
        {
            lock (_gate)
            {
                return _active?.DisplayName;
            }
        }
    }

    public TimeSpan Uptime
    {
        get
        {
            lock (_gate)
            {
                return _active is null ? TimeSpan.Zero : _clock.GetUtcNow() - _active.StartedAt;
            }
        }
    }

    public async Task<HostStartResult> StartAsync(
        string projectFolder,
        string displayName,
        int port,
        CancellationToken cancellationToken)
    {
        CancellationTokenSource start;
        lock (_gate)
        {
            if (_active is not null || _pending is not null)
            {
                return HostStartResult.Of(
                    HostStartKind.AlreadyRunning,
                    "A game is already being hosted. Stop it before hosting another.");
            }

            start = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            _pending = start;
        }

        try
        {
            return await StartCoreAsync(projectFolder, displayName, port, start);
        }
        finally
        {
            lock (_gate)
            {
                if (ReferenceEquals(_pending, start))
                {
                    _pending = null;
                }
            }

            start.Dispose();
        }
    }

    // Ends the running session, or cancels a start in progress (which kills the server it launched).
    // Returns false when there was neither. Never raises Ended.
    public bool Stop()
    {
        ActiveSession? session;
        lock (_gate)
        {
            session = _active;
            _active = null;
            if (session is null)
            {
                _pending?.Cancel();
                return _pending is not null;
            }
        }

        // _active is already cleared, so the watch ignores the exit this kill causes and disposes the process.
        session.Process.Kill();
        session.Cancel.Cancel();
        return true;
    }

    // Returns an error message, or null when the window was started.
    public string? OpenAnotherWindow()
    {
        var port = Port;
        if (port is null)
        {
            return "No game is being hosted.";
        }

        var result = _launcher.JoinGame(LocalHost, port.Value);
        result.Process?.Dispose();
        return result.Error;
    }

    private async Task<HostStartResult> StartCoreAsync(
        string projectFolder,
        string displayName,
        int port,
        CancellationTokenSource start)
    {
        var cancellationToken = start.Token;
        bool inUse;
        try
        {
            // The server exits with one generic failure code whatever went wrong, so a port that already
            // answers before we start anything is the only way to give port-in-use its own message.
            inUse = await _probe.AcceptsConnectionAsync(LocalHost, port, cancellationToken);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return CancelledResult();
        }

        if (inUse)
        {
            return HostStartResult.Of(
                HostStartKind.PortInUse,
                $"Port {port} is already in use; choose another port in Settings.");
        }

        var launch = _launcher.StartServer(projectFolder, port);
        if (!launch.Succeeded)
        {
            return HostStartResult.Of(HostStartKind.LaunchFailed, launch.Error);
        }

        var server = launch.Process!;
        var failure = await WaitUntilListeningAsync(server, port, cancellationToken);
        if (failure is not null)
        {
            return failure;
        }

        var session = new ActiveSession(server, displayName, port, _clock.GetUtcNow());
        var token = session.Cancel.Token;
        bool cancelled;
        lock (_gate)
        {
            cancelled = start.IsCancellationRequested;
            if (!cancelled)
            {
                _active = session;
                _pending = null;
            }
        }

        if (cancelled)
        {
            server.Kill();
            server.Dispose();
            return CancelledResult();
        }

        _ = WatchAsync(session, token);
        return OpenHostWindow(session, port);
    }

    // A failed game window leaves the server running: others may still join, and the UI can retry.
    private HostStartResult OpenHostWindow(ActiveSession session, int port)
    {
        var window = _launcher.JoinGame(LocalHost, port);
        window.Process?.Dispose();

        int? exitCode;
        lock (_gate)
        {
            exitCode = session.ExitCode;
        }

        if (exitCode is not null)
        {
            return ServerExitedResult(exitCode.Value);
        }

        return window.Succeeded
            ? HostStartResult.Of(HostStartKind.Started, null)
            : HostStartResult.Of(HostStartKind.StartedWithoutWindow, window.Error);
    }

    private static HostStartResult CancelledResult() =>
        HostStartResult.Of(HostStartKind.Cancelled, "Starting the server was cancelled.");

    // Null when the server is listening; otherwise the failure, with the server already cleaned up.
    // Any other exception from the probe or the delay kills the server too and propagates.
    private async Task<HostStartResult?> WaitUntilListeningAsync(
        IRunningProcess server,
        int port,
        CancellationToken cancellationToken)
    {
        var begun = _clock.GetTimestamp();
        try
        {
            while (true)
            {
                if (server.HasExited)
                {
                    return ServerExited(server);
                }

                if (await _probe.AcceptsConnectionAsync(LocalHost, port, cancellationToken))
                {
                    return server.HasExited ? ServerExited(server) : null;
                }

                if (_clock.GetElapsedTime(begun) >= _window)
                {
                    return TimedOut(server);
                }

                await _delay(_pollInterval, cancellationToken);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            server.Kill();
            server.Dispose();
            return CancelledResult();
        }
        catch (Exception)
        {
            server.Kill();
            server.Dispose();
            throw;
        }
    }

    private HostStartResult ServerExited(IRunningProcess server)
    {
        var code = server.ExitCode ?? -1;
        server.Dispose();
        return ServerExitedResult(code);
    }

    private HostStartResult ServerExitedResult(int code)
    {
        var log = _userData.LogFile(EngineExecutable.Server);
        return new HostStartResult(
            HostStartKind.ServerExited,
            $"The server exited with code {code.ToString(CultureInfo.InvariantCulture)}. See {log}.",
            log);
    }

    private HostStartResult TimedOut(IRunningProcess server)
    {
        var log = _userData.LogFile(EngineExecutable.Server);
        server.Kill();
        server.Dispose();
        var count = (int)Math.Round(_window.TotalSeconds);
        var unit = count == 1 ? "second" : "seconds";
        var seconds = count.ToString(CultureInfo.InvariantCulture);
        return new HostStartResult(
            HostStartKind.TimedOut,
            $"The server did not start listening within {seconds} {unit}. See {log}.",
            log);
    }

    // Owns the process once the session is published: disposes it when the wait ends, by exit or by Stop.
    private async Task WatchAsync(ActiveSession session, CancellationToken token)
    {
        int code;
        try
        {
            code = await session.Process.WaitForExitAsync(token);
        }
        catch (OperationCanceledException)
        {
            session.Process.Dispose();
            return;
        }

        bool stillActive;
        lock (_gate)
        {
            stillActive = ReferenceEquals(_active, session);
            if (stillActive)
            {
                _active = null;
                session.ExitCode = code;
            }
        }

        session.Process.Dispose();
        if (stillActive)
        {
            Ended?.Invoke(new HostSessionEnd(code, _userData.LogFile(EngineExecutable.Server)));
        }
    }

    private sealed class ActiveSession
    {
        public ActiveSession(IRunningProcess process, string displayName, int port, DateTimeOffset startedAt)
        {
            Process = process;
            DisplayName = displayName;
            Port = port;
            StartedAt = startedAt;
        }

        public IRunningProcess Process { get; }

        public string DisplayName { get; }

        public int Port { get; }

        public DateTimeOffset StartedAt { get; }

        public CancellationTokenSource Cancel { get; } = new();

        // Set under the host's lock when the server exits by itself.
        public int? ExitCode { get; set; }
    }
}
