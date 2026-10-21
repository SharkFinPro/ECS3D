namespace ECS3DLauncher.Services;

public enum HostStartKind
{
    Started,
    StartedWithoutWindow,
    AlreadyRunning,
    PortInUse,
    LaunchFailed,
    ServerExited,
    TimedOut,
    Cancelled,
}

// LogPath is set when the server ran and failed (ServerExited, TimedOut), for the toast's Open logs button.
public sealed record HostStartResult(HostStartKind Kind, string? Message, string? LogPath)
{
    // StartedWithoutWindow is a running session whose own game window failed to open; Message says why.
    public bool Succeeded => Kind is HostStartKind.Started or HostStartKind.StartedWithoutWindow;

    public static HostStartResult Of(HostStartKind kind, string? message) => new(kind, message, null);
}

public sealed record HostSessionEnd(int? ExitCode, string LogPath);
