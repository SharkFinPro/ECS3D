using System;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;

namespace ECS3DLauncher.Services;

// One hosted server at a time, owned by the launcher. The UI calls it from its own thread; Ended is raised
// from a thread-pool continuation, so a handler must marshal to the UI thread itself. Nothing stops the
// server on its own (not even disposal): quitting while hosting is the UI's prompt, so it reads IsRunning.
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
    private bool _starting;

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

    // Raised when the server exits on its own, with any exit code. Not raised by Stop.
    public event Action<HostSessionEnd>? Ended;

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
        lock (_gate)
        {
            if (_active is not null || _starting)
            {
                return HostStartResult.Of(
                    HostStartKind.AlreadyRunning,
                    "A game is already being hosted. Stop it before hosting another.");
            }

            _starting = true;
        }

        try
        {
            return await StartCoreAsync(projectFolder, displayName, port, cancellationToken);
        }
        finally
        {
            lock (_gate)
            {
                _starting = false;
            }
        }
    }

    // Returns false when nothing was being hosted. Never raises Ended.
    public bool Stop()
    {
        ActiveSession? session;
        lock (_gate)
        {
            session = _active;
            _active = null;
        }

        if (session is null)
        {
            return false;
        }

        // The watch is cancelled before the kill, or the kill would read as a crash.
        session.Cancel.Cancel();
        session.Process.Kill();
        session.Dispose();
        return true;
    }

    public LaunchResult OpenAnotherWindow()
    {
        var port = Port;
        if (port is null)
        {
            return LaunchResult.Failed("No game is being hosted.");
        }

        var result = _launcher.JoinGame(LocalHost, port.Value);
        result.Process?.Dispose();
        return result;
    }

    private async Task<HostStartResult> StartCoreAsync(
        string projectFolder,
        string displayName,
        int port,
        CancellationToken cancellationToken)
    {
        // The server exits with one generic failure code whatever went wrong, so a port that already
        // answers before we start anything is the only way to give port-in-use its own message.
        if (await _probe.AcceptsConnectionAsync(LocalHost, port, cancellationToken))
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
        lock (_gate)
        {
            _active = session;
        }

        _ = WatchAsync(session);

        // A failed game window leaves the server running: others may still join, and the UI can retry.
        var window = _launcher.JoinGame(LocalHost, port);
        if (!window.Succeeded)
        {
            return HostStartResult.Of(HostStartKind.StartedWithoutWindow, window.Error);
        }

        window.Process?.Dispose();
        return HostStartResult.Of(HostStartKind.Started, null);
    }

    // Null when the server is listening; otherwise the failure, with the server already cleaned up.
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
            return HostStartResult.Of(HostStartKind.Cancelled, "Starting the server was cancelled.");
        }
    }

    private HostStartResult ServerExited(IRunningProcess server)
    {
        var code = server.ExitCode ?? -1;
        var log = _userData.LogFile(EngineExecutable.Server);
        server.Dispose();
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
        var seconds = ((int)Math.Round(_window.TotalSeconds)).ToString(CultureInfo.InvariantCulture);
        return new HostStartResult(
            HostStartKind.TimedOut,
            $"The server did not start listening within {seconds} seconds. See {log}.",
            log);
    }

    private async Task WatchAsync(ActiveSession session)
    {
        int code;
        try
        {
            code = await session.Process.WaitForExitAsync(session.Cancel.Token);
        }
        catch (OperationCanceledException)
        {
            return;
        }

        lock (_gate)
        {
            if (!ReferenceEquals(_active, session))
            {
                return;
            }

            _active = null;
        }

        session.Dispose();
        Ended?.Invoke(new HostSessionEnd(code, _userData.LogFile(EngineExecutable.Server)));
    }

    private sealed class ActiveSession : IDisposable
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

        public void Dispose()
        {
            Cancel.Dispose();
            Process.Dispose();
        }
    }
}
