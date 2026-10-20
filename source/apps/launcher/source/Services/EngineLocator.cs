using System;
using System.Collections.Generic;
using System.IO;

namespace ECS3DLauncher.Services;

public sealed class EngineLocator
{
    private readonly IFileSystem _fileSystem;
    private readonly HostOs _os;
    private readonly string _launcherDirectory;

    public EngineLocator(IFileSystem fileSystem)
        : this(fileSystem, HostOsDetector.Current(), AppContext.BaseDirectory)
    {
    }

    public EngineLocator(IFileSystem fileSystem, HostOs os, string launcherDirectory)
    {
        _fileSystem = fileSystem;
        _os = os;
        _launcherDirectory = launcherDirectory;
    }

    public EngineLocation Locate(string? overrideFolder)
    {
        var chosen = string.IsNullOrWhiteSpace(overrideFolder) ? _launcherDirectory : overrideFolder;
        var folder = Path.GetFullPath(chosen);
        var found = new Dictionary<EngineExecutable, string>();
        var missing = new List<MissingExecutable>();

        foreach (var executable in Enum.GetValues<EngineExecutable>())
        {
            var fileName = EngineExecutableNames.FileName(executable, _os);
            var path = Path.Combine(folder, fileName);
            if (_fileSystem.FileExists(path))
            {
                found[executable] = path;
            }
            else
            {
                missing.Add(new MissingExecutable(executable, fileName, path));
            }
        }

        return new EngineLocation(folder, found, missing);
    }
}
