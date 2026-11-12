namespace ECS3DLauncher.Services;

public enum LauncherTab
{
    Projects,
    Library,
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
        LauncherTab.Library => "library",
        LauncherTab.Learn => "learn",
        LauncherTab.Settings => "settings",
        _ => "projects",
    };

    // The old "templates" and "samples" keys land on Library, which replaced both tabs.
    // An unknown key falls back to Projects, the tab the launcher opens on.
    public static LauncherTab Parse(string? key) => key switch
    {
        "library" or "templates" or "samples" => LauncherTab.Library,
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
        LauncherTab.Library => new TabHeader(
            "Library",
            "Start from a template, run a finished sample, or take the Welcome tour.",
            "Search the library",
            true,
            false),
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
