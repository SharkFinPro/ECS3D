namespace ECS3DLauncher.Services;

public enum LauncherTab
{
    Projects,
    Templates,
    Samples,
    Learn,
    Settings,
}

public sealed record TabHeader(
    string Title,
    string Subtitle,
    string SearchPlaceholder,
    bool ShowSearch,
    bool ShowViewToggle);

public static class LauncherTabs
{
    public static string Key(LauncherTab tab) => tab switch
    {
        LauncherTab.Templates => "templates",
        LauncherTab.Samples => "samples",
        LauncherTab.Learn => "learn",
        LauncherTab.Settings => "settings",
        _ => "projects",
    };

    // An unknown key falls back to Projects, the tab the launcher opens on.
    public static LauncherTab Parse(string? key) => key switch
    {
        "templates" => LauncherTab.Templates,
        "samples" => LauncherTab.Samples,
        "learn" => LauncherTab.Learn,
        "settings" => LauncherTab.Settings,
        _ => LauncherTab.Projects,
    };

    // With no stored projects the Projects tab greets instead of listing, so the grid/list toggle has nothing to do.
    public static TabHeader Header(LauncherTab tab, bool noProjects) =>
        tab == LauncherTab.Projects && noProjects
            ? Header(tab) with { Title = "Welcome to ECS3D", ShowViewToggle = false }
            : Header(tab);

    public static TabHeader Header(LauncherTab tab) => tab switch
    {
        LauncherTab.Templates => new TabHeader(
            "Templates", "Start a project from a ready-made scene.", "Search templates", true, false),
        LauncherTab.Samples => new TabHeader(
            "Samples", "Example projects bundled with ECS3D.", "Search samples", true, false),
        LauncherTab.Learn => new TabHeader(
            "Learn", "Guides and documentation for the engine.", "Search guides", true, false),
        LauncherTab.Settings => new TabHeader(
            "Settings",
            "Launcher preferences. Editor preferences live in the editor's own Settings.",
            "",
            false,
            false),
        _ => new TabHeader(
            "Recent Projects", "Pick up where you left off, or start something new.", "Search projects", true, true),
    };
}
