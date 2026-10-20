using System;

namespace ECS3DLauncher.Services;

public enum EngineExecutable
{
    Editor,
    Client,
    Server,
}

public static class EngineExecutableNames
{
    public static string BaseName(EngineExecutable executable) => executable switch
    {
        EngineExecutable.Editor => "ECS3DEditor",
        EngineExecutable.Client => "ECS3DClient",
        EngineExecutable.Server => "ECS3DServer",
        _ => throw new ArgumentOutOfRangeException(nameof(executable), executable, null),
    };

    public static string FileName(EngineExecutable executable, HostOs os) =>
        os == HostOs.Windows ? BaseName(executable) + ".exe" : BaseName(executable);

    // The log file stem each engine app writes under <user data>/logs.
    public static string LogName(EngineExecutable executable) => executable switch
    {
        EngineExecutable.Editor => "editor",
        EngineExecutable.Client => "client",
        EngineExecutable.Server => "server",
        _ => throw new ArgumentOutOfRangeException(nameof(executable), executable, null),
    };
}
