using System;
using System.IO;
using System.Linq;

namespace ECS3DLauncher.Services;

// What one change did: whether it was saved, whether the save failed, and the engine location when the
// change moved the engine folder (null otherwise).
public sealed record SettingsUpdate(bool Saved, bool SaveFailed, EngineLocation? Engine)
{
    public static SettingsUpdate None { get; } = new(false, false, null);
}

// The Settings tab's decisions without any UI: applies one change at a time, validates input, re-locates the
// engine when its folder changes and saves through the store at once. A change that fails to save stays applied.
public sealed class SettingsEditor
{
    private readonly LauncherSettingsStore _store;
    private readonly EngineLocator _locator;
    private LauncherSettings _settings;

    public SettingsEditor(LauncherSettingsStore store, EngineLocator locator)
    {
        _store = store;
        _locator = locator;
        _settings = store.Load();
        Engine = TryLocate(_settings.EngineDirectory, out var location) ? location! : locator.Locate(null);
        EngineError = location is null ? InvalidFolder : null;
    }

    public LauncherSettings Current => _settings;

    public string FilePath => _store.FilePath;

    public EngineLocation Engine { get; private set; }

    public string? EngineError { get; private set; }

    public string? PortError { get; private set; }

    public string EngineFolderText => _settings.EngineDirectory ?? "";

    public string LastJoinText => _settings.LastJoin is { } join ? $"{join.Address}:{join.Port}" : "-";

    // The executables the located folder holds, or null when it holds none.
    public string? EngineFoundText
    {
        get
        {
            var names = Engine.Found.Values.Select(Path.GetFileName).ToList();
            return names.Count == 0 ? null : $"Found {string.Join(", ", names)} in {Engine.Folder}";
        }
    }

    public string? EngineMissingText => Engine.Describe();

    private static string InvalidFolder => "That is not a valid folder path.";

    public SettingsUpdate SetEngineDirectory(string? text)
    {
        var folder = string.IsNullOrWhiteSpace(text) ? null : text.Trim();
        if (!TryLocate(folder, out var location))
        {
            EngineError = InvalidFolder;
            return SettingsUpdate.None;
        }

        EngineError = null;
        Engine = location!;
        if (folder == _settings.EngineDirectory)
        {
            return new SettingsUpdate(false, false, location);
        }

        return Commit(_settings with { EngineDirectory = folder }, location);
    }

    public SettingsUpdate ResetEngineDirectory() => SetEngineDirectory(null);

    // An empty location goes back to the default folder.
    public SettingsUpdate SetProjectsDirectory(string? text)
    {
        var folder = string.IsNullOrWhiteSpace(text) ? _store.Defaults().ProjectsDirectory : text.Trim();
        return folder == _settings.ProjectsDirectory
            ? SettingsUpdate.None
            : Commit(_settings with { ProjectsDirectory = folder }, null);
    }

    public SettingsUpdate SetEditorConsole(bool value) =>
        value == _settings.EditorConsole ? SettingsUpdate.None : Commit(_settings with { EditorConsole = value }, null);

    public SettingsUpdate SetServerConsole(bool value) =>
        value == _settings.ServerConsole ? SettingsUpdate.None : Commit(_settings with { ServerConsole = value }, null);

    public SettingsUpdate SetCloseAfterOpen(bool value) =>
        value == _settings.CloseAfterOpen
            ? SettingsUpdate.None
            : Commit(_settings with { CloseAfterOpen = value }, null);

    // An invalid port is reported through PortError and never saved; a valid one clears the error.
    public SettingsUpdate SetHostPort(string? text)
    {
        if (!SettingsInput.TryParsePort(text, out var port))
        {
            PortError = $"Enter a port from {SettingsInput.MinPort} to {SettingsInput.MaxPort}.";
            return SettingsUpdate.None;
        }

        PortError = null;
        return port == _settings.HostPort ? SettingsUpdate.None : Commit(_settings with { HostPort = port }, null);
    }

    public SettingsUpdate SetLastJoin(JoinTarget target)
    {
        var join = new LastJoin(target.Address, target.Port);
        return join == _settings.LastJoin ? SettingsUpdate.None : Commit(_settings with { LastJoin = join }, null);
    }

    public SettingsUpdate SetProjectsView(bool grid)
    {
        var view = grid ? LauncherSettings.GridView : LauncherSettings.ListView;
        return view == _settings.ProjectsView ? SettingsUpdate.None : Commit(_settings with { ProjectsView = view }, null);
    }

    private SettingsUpdate Commit(LauncherSettings next, EngineLocation? engine)
    {
        _settings = next;
        var saved = _store.Save(next);
        return new SettingsUpdate(saved, !saved, engine);
    }

    private bool TryLocate(string? folder, out EngineLocation? location)
    {
        try
        {
            location = _locator.Locate(folder);
            return true;
        }
        catch (Exception exception) when (exception is ArgumentException or NotSupportedException or IOException)
        {
            location = null;
            return false;
        }
    }
}
