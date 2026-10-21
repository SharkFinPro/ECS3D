namespace ECS3DLauncher.Services;

public enum EarlyExitKind
{
    ExitedEarly,
    ExitedCleanly,
    StillRunning,
    Cancelled,
}

// ExitCode and LogPath are set only for ExitedEarly (ExitCode is also 0 for ExitedCleanly).
public sealed record EarlyExitResult(EarlyExitKind Kind, EngineExecutable Executable, int? ExitCode, string? LogPath);
