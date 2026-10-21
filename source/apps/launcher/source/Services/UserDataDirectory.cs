using System;
using System.Collections.Generic;
using System.IO;

namespace ECS3DLauncher.Services;

// Mirrors userDataDirectory() in the engine's source/libs/log/UserDataDirectory.cpp.
public sealed class UserDataDirectory
{
    private const string AppFolder = "ECS3D";

    private readonly HostOs _os;
    private readonly Func<string, string?> _getEnvironmentVariable;

    public UserDataDirectory()
        : this(HostOsDetector.Current(), Environment.GetEnvironmentVariable)
    {
    }

    public UserDataDirectory(HostOs os, Func<string, string?> getEnvironmentVariable)
    {
        _os = os;
        _getEnvironmentVariable = getEnvironmentVariable;
    }

    public string Resolve()
    {
        switch (_os)
        {
            case HostOs.Windows:
                return Under("APPDATA") ?? AppFolder;
            case HostOs.MacOs:
                return Under("HOME", "Library", "Application Support") ?? AppFolder;
            default:
                return Under("XDG_CONFIG_HOME") ?? Under("HOME", ".config") ?? AppFolder;
        }
    }

    public string LogFile(EngineExecutable executable) =>
        Path.Combine(Resolve(), "logs", EngineExecutableNames.LogName(executable) + ".log");

    public string LauncherLogFile() => Path.Combine(Resolve(), "logs", "launcher.log");

    public string SettingsFile() => Path.Combine(Resolve(), "launcher-settings.json");

    // An empty variable is as useless as an unset one.
    private string? Under(string variable, params string[] subfolders)
    {
        var value = _getEnvironmentVariable(variable);
        if (string.IsNullOrEmpty(value))
        {
            return null;
        }

        var parts = new List<string> { value };
        parts.AddRange(subfolders);
        parts.Add(AppFolder);
        return Path.Combine(parts.ToArray());
    }
}
