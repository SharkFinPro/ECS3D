using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace ECS3DLauncher.Services;

public sealed class LauncherSettingsStore
{
    private const int SchemaVersion = 1;
    private const string ProjectsFolderName = "ECS3D Projects";

    private static readonly JsonSerializerOptions Pretty = new() { WriteIndented = true };

    private readonly IFileSystem _fileSystem;
    private readonly ILauncherLog _log;
    private readonly string _path;
    private readonly Func<string> _documents;
    private JsonObject _root = new();

    public LauncherSettingsStore(IFileSystem fileSystem, ILauncherLog log, UserDataDirectory userData)
        : this(fileSystem, log, userData, () => Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments))
    {
    }

    public LauncherSettingsStore(
        IFileSystem fileSystem,
        ILauncherLog log,
        UserDataDirectory userData,
        Func<string> documents)
    {
        _fileSystem = fileSystem;
        _log = log;
        _path = userData.SettingsFile();
        _documents = documents;
    }

    public string FilePath => _path;

    public LauncherSettings Defaults() =>
        new() { ProjectsDirectory = Path.Combine(_documents(), ProjectsFolderName) };

    public LauncherSettings Load()
    {
        _root = new JsonObject();
        if (!_fileSystem.FileExists(_path))
        {
            return Defaults();
        }

        var root = ReadRoot();
        if (root is null)
        {
            return Defaults();
        }

        _root = root;
        return Parse(new SettingsFieldReader(root, _path, _log));
    }

    public bool Save(LauncherSettings settings)
    {
        try
        {
            Apply(settings);
            var directory = Path.GetDirectoryName(_path);
            if (!string.IsNullOrEmpty(directory))
            {
                _fileSystem.CreateDirectory(directory);
            }

            _fileSystem.WriteAllTextAtomic(_path, _root.ToJsonString(Pretty));
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            _log.Warn($"{_path}: could not save settings: {ex.Message}");
            return false;
        }
    }

    private JsonObject? ReadRoot()
    {
        try
        {
            var node = JsonNode.Parse(_fileSystem.ReadAllText(_path));
            if (node is JsonObject root)
            {
                return root;
            }

            _log.Warn($"{_path}: the top level is not an object; using defaults");
        }
        catch (Exception ex) when (ex is JsonException or IOException or UnauthorizedAccessException)
        {
            _log.Warn($"{_path}: could not read settings: {ex.Message}; using defaults");
        }

        return null;
    }

    private LauncherSettings Parse(SettingsFieldReader reader)
    {
        var defaults = Defaults();
        return new LauncherSettings
        {
            EngineDirectory = reader.ReadNullableString("engineDirectory", defaults.EngineDirectory),
            ProjectsDirectory = reader.ReadString("projectsDirectory", defaults.ProjectsDirectory),
            EditorConsole = reader.ReadBool("editorConsole", defaults.EditorConsole),
            ServerConsole = reader.ReadBool("serverConsole", defaults.ServerConsole),
            CloseAfterOpen = reader.ReadBool("closeAfterOpen", defaults.CloseAfterOpen),
            HostPort = reader.ReadPort("hostPort", defaults.HostPort),
            LastJoin = reader.ReadLastJoin("lastJoin"),
            ProjectsView = reader.ReadChoice(
                "projectsView", defaults.ProjectsView, LauncherSettings.GridView, LauncherSettings.ListView),
        };
    }

    private void Apply(LauncherSettings settings)
    {
        _root["version"] = SchemaVersion;
        _root["engineDirectory"] = settings.EngineDirectory;
        _root["projectsDirectory"] = settings.ProjectsDirectory;
        _root["editorConsole"] = settings.EditorConsole;
        _root["serverConsole"] = settings.ServerConsole;
        _root["closeAfterOpen"] = settings.CloseAfterOpen;
        _root["hostPort"] = settings.HostPort;
        _root["projectsView"] = settings.ProjectsView;
        ApplyLastJoin(settings.LastJoin);
    }

    // Edits the existing object in place so fields this version does not know survive.
    private void ApplyLastJoin(LastJoin? join)
    {
        if (join is null)
        {
            _root.Remove("lastJoin");
            return;
        }

        if (_root["lastJoin"] is JsonObject existing)
        {
            existing["address"] = join.Address;
            existing["port"] = join.Port;
            return;
        }

        _root["lastJoin"] = new JsonObject { ["address"] = join.Address, ["port"] = join.Port };
    }
}
