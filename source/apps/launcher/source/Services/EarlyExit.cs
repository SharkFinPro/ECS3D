namespace ECS3DLauncher.Services;

public sealed record EarlyExit(EngineExecutable Executable, int ExitCode, string LogPath);
