namespace ECS3DLauncher.Services;

// One row of recent-projects.json. Fields the launcher does not know stay in the store, not here.
public sealed record RecentProject(string Path, string Name, string LastOpenedUtc, bool Pinned, string? Template);
