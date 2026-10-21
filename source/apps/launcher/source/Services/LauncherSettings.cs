namespace ECS3DLauncher.Services;

public sealed record LastJoin(string Address, int Port);

public sealed record LauncherSettings
{
    public static string GridView => "grid";
    public static string ListView => "list";

    // Null or empty means the launcher's own folder.
    public string? EngineDirectory { get; init; }

    public string ProjectsDirectory { get; init; } = "";

    public bool EditorConsole { get; init; }

    public bool ServerConsole { get; init; }

    public bool CloseAfterOpen { get; init; }

    public int HostPort { get; init; } = EnginePorts.Default;

    public LastJoin? LastJoin { get; init; }

    public string ProjectsView { get; init; } = GridView;

    public LaunchOptions ToLaunchOptions() => new(EditorConsole, ServerConsole);
}
